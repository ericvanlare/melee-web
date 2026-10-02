/*
 * Bounded local runtime incident diagnostics.
 *
 * This module is deliberately a recorder, not a logger.  Native callbacks
 * publish scalar values through observeNative's positional form.  The hot
 * path writes one row in a fixed Float64Array and updates scalar maxima;
 * object snapshots are made only for an explicit trigger or a bounded event.
 * No network, console, save-data, input, memory, URL, or arbitrary error data
 * is accepted by this module.
 */

export const RUNTIME_DIAGNOSTICS_SCHEMA = 'melee-web-runtime-diagnostics';
export const RUNTIME_DIAGNOSTICS_VERSION = 1;
export const DEFAULT_HISTORY_SECONDS = 10;
export const DEFAULT_HISTORY_HZ = 10;
export const DEFAULT_MAX_INCIDENTS = 4;
export const DEFAULT_MAX_REPORT_BYTES = 64 * 1024;
export const DEFAULT_MAX_STORAGE_BYTES = 256 * 1024;
export const DEFAULT_POST_EVENT_CAP = 24;
export const DEFAULT_RECENT_EVENT_CAP = 32;
export const DEFAULT_POST_WINDOW_MS = 1000;
export const DIAGNOSTICS_DB_NAME = 'melee-web-runtime-diagnostics';
export const DIAGNOSTICS_STORE_NAME = 'incidents';

const REASON_NAMES = Object.freeze({
  1: 'simulation_debt',
  2: 'audio_debt',
  3: 'nonfinite_clock',
  4: 'runtime_failure',
  5: 'manual_pause',
  6: 'manual_resume',
  7: 'render_preparation',
  8: 'clock_regression',
  9: 'scheduled_pause',
});
const REASON_CODES = Object.freeze(Object.fromEntries(
  Object.entries(REASON_NAMES).map(([code, name]) => [name, Number(code)]),
));
const CLOCK_OWNER_NAMES = Object.freeze({0: 'other', 1: 'simulation', 2: 'audio'});
const SCENE_NAMES = Object.freeze({
  0: 'unknown', 1: 'css', 2: 'preparing', 3: 'sss', 4: 'preparing',
  5: 'preparing', 6: 'unloaded', 7: 'match', 8: 'results', 9: 'prize',
  10: 'title', 11: 'main', 12: 'opening', 13: 'opening_vs',
});
const LIFECYCLE_NAMES = new Set([
  'active', 'inactive', 'pause', 'resume', 'visibility_hidden',
  'visibility_visible', 'pagehide', 'pageshow', 'scene_enter', 'scene_exit',
  'recovered', 'recovery', 'timeout', 'unknown',
  'scheduled_pause', 'render_preparation', 'freeze', 'preparation', 'preparation_done',
  'asset_preparation',
]);
const PROFILE_RE = /^[a-z0-9][a-z0-9._-]{0,31}$/;
const HASH_RE = /^[a-f0-9]{7,128}$/i;
const HOST_HEX_RE = /^[0-9a-f]+$/;
let SESSION_SEQUENCE = 0;
const MAX_STORED_RECORD_BYTES = 64 * 1024;
const MAX_STORED_CURSOR_RECORDS = 64;
const HISTORY_COLUMNS = Object.freeze([
  'timestamp', 'source_frame', 'scene', 'stage', 'fighter0', 'fighter1',
  'fighter2', 'fighter3', 'debt_ticks', 'update_ms', 'draw_ms', 'total_ms',
  'preparation_ms', 'queued_delta', 'created_delta', 'texture_upload_bytes',
  'source_steps', 'source_draws', 'running', 'interval_ms',
]);
const HISTORY_COLUMN_COUNT = HISTORY_COLUMNS.length;
const HISTORY_INDEX = Object.freeze(Object.fromEntries(
  HISTORY_COLUMNS.map((name, index) => [name, index]),
));

const finite = value => typeof value === 'number' && Number.isFinite(value) ? value : null;
const integer = value => finite(value) !== null && Number.isSafeInteger(value) ? value : null;
const maxNullable = (current, value) => value === null ? current : current === null ? value : Math.max(current, value);
const sumNullable = (current, value) => value === null ? current : current === null ? value : current + value;
const scratchValue = value => value === null ? NaN : value;

function nativeNumber(value, unavailableSentinel = false) {
  return unavailableSentinel && value === -1 ? null : finite(value);
}
function invalidNativeNumber(value, unavailableSentinel = false) {
  return value !== undefined && value !== null &&
    !(unavailableSentinel && value === -1) && finite(value) === null;
}

function clampPositiveInteger(value, fallback, maximum) {
  const n = Number(value);
  return Number.isInteger(n) && n > 0 ? Math.min(n, maximum) : fallback;
}

function clampNonNegative(value, fallback, maximum) {
  const n = Number(value);
  return Number.isFinite(n) && n >= 0 ? Math.min(n, maximum) : fallback;
}

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

function deepFreeze(value, seen = new WeakSet()) {
  if (value === null || typeof value !== 'object' || seen.has(value)) return value;
  seen.add(value);
  for (const item of Object.values(value)) deepFreeze(item, seen);
  return Object.freeze(value);
}

function immutable(value) {
  return deepFreeze(clone(value));
}

function normalizeHash(value, minimum = 7, maximum = 128) {
  const text = typeof value === 'string' ? value.trim() : '';
  return HASH_RE.test(text) && text.length >= minimum && text.length <= maximum ? text.toLowerCase() : null;
}

function normalizeProfile(value) {
  const text = typeof value === 'string' ? value.trim().toLowerCase() : '';
  return PROFILE_RE.test(text) ? text : 'unknown';
}

function normalizeIdentity(identity) {
  const source = identity && typeof identity === 'object' ? identity : {};
  return Object.freeze({
    schema_version: 1,
    source_commit: normalizeHash(source.sourceCommit ?? source.source_commit ?? source.commit, 40, 40),
    runtime_hash: normalizeHash(source.runtimeHash ?? source.runtime_hash ?? source.runtime, 16, 16),
    build_profile: normalizeProfile(source.buildProfile ?? source.build_profile ?? source.profile),
  });
}

function hostEnvironment(origin) {
  let host = '';
  let protocol = '';
  let normalizedOrigin = null;
  try {
    const parsed = new URL(typeof origin === 'string' ? origin : '');
    host = parsed.hostname.toLowerCase();
    protocol = parsed.protocol.toLowerCase();
    normalizedOrigin = parsed.origin;
  } catch {
    return {kind: 'unknown', host: null, origin: null, secure: false};
  }

  const productionExact = new Set(['webmelee.gg', 'www.webmelee.gg', 'webmelee.pages.dev']);
  const stagingExact = new Set(['webmelee-staging.pages.dev', 'staging.webmelee.gg']);
  const isHexDeployment = (suffix) => {
    if (!host.endsWith(suffix)) return false;
    const prefix = host.slice(0, -suffix.length);
    return prefix.length === 8 && HOST_HEX_RE.test(prefix);
  };
  if (productionExact.has(host) || isHexDeployment('.webmelee.pages.dev')) {
    return {kind: 'production', host, origin: normalizedOrigin, secure: protocol === 'https:'};
  }
  if (stagingExact.has(host) || isHexDeployment('.webmelee-staging.pages.dev')) {
    return {kind: 'staging', host, origin: normalizedOrigin, secure: protocol === 'https:'};
  }
  if (host === 'localhost' || host === '127.0.0.1' || host === '[::1]' || host === '::1') {
    return {kind: 'local', host, origin: normalizedOrigin, secure: protocol === 'https:'};
  }
  return {kind: 'unknown', host: null, origin: normalizedOrigin, secure: false};
}

function coarseClient(root) {
  const navigatorValue = root.navigator || {};
  const ua = typeof navigatorValue.userAgent === 'string' ? navigatorValue.userAgent : '';
  const platform = typeof navigatorValue.platform === 'string' ? navigatorValue.platform.toLowerCase() : '';
  let platformName = 'unknown';
  if (/android/i.test(ua) || /android/.test(platform)) platformName = 'android';
  else if (/mac|iphone|ipad|ipod/.test(platform) || /mac os|iphone|ipad|ipod/i.test(ua)) platformName = /iphone|ipad|ipod/i.test(ua) ? 'ios' : 'macos';
  else if (/win/.test(platform) || /windows/i.test(ua)) platformName = 'windows';
  else if (/linux|x11/.test(platform) || /linux/i.test(ua)) platformName = 'linux';
  let browser = 'unknown';
  let major = null;
  const browserVersions = [
    ['edge', /(?:Edg|EdgA|EdgiOS)\/(\d+)/i],
    ['opera', /(?:OPR|Opera)\/(\d+)/i],
    ['chrome', /(?:Chrome|CriOS)\/(\d+)/i],
    ['firefox', /(?:Firefox|FxiOS)\/(\d+)/i],
    ['safari', /Version\/(\d+)/i],
  ];
  for (const [name, pattern] of browserVersions) {
    const match = ua.match(pattern);
    if (match) { browser = name; major = Number(match[1]); break; }
  }
  return {platform: platformName, browser, browser_major: Number.isSafeInteger(major) ? major : null};
}

function ephemeralSessionId(root) {
  try {
      const cryptoSource = root.crypto;
      if (cryptoSource && typeof cryptoSource.getRandomValues === 'function') {
        const bytes = new Uint8Array(16);
        const result = cryptoSource.getRandomValues(bytes);
        if (!(result instanceof Uint8Array) || result.length !== bytes.length) throw new Error('crypto unavailable');
        return {id: `session-${Array.from(bytes, byte => byte.toString(16).padStart(2, '0')).join('')}`, fallback: false};
    }
  } catch {
    // Fall through to a process-local random value and expose the limitation.
  }
  const random = `${Date.now().toString(36)}-${Math.random().toString(36).slice(2)}-${(++SESSION_SEQUENCE).toString(36)}`;
  return {id: `session-${random}`, fallback: true};
}

function normalizeReason(value) {
  if (typeof value === 'string') {
    const name = value.trim().toLowerCase();
    const code = REASON_CODES[name];
    return code ? {code, name, malformed: false} : {code: 0, name: 'unknown', malformed: true};
  }
  const code = integer(value);
  return REASON_NAMES[code]
    ? {code, name: REASON_NAMES[code], malformed: false}
    : {code: 0, name: 'unknown', malformed: true};
}

function normalizeClockOwner(value) {
  const code = integer(value);
  return CLOCK_OWNER_NAMES[code] ? {code, name: CLOCK_OWNER_NAMES[code], malformed: false}
    : {code: 0, name: 'other', malformed: value !== undefined && value !== null};
}

function normalizeScene(value) {
  if (typeof value === 'string') {
    const text = value.trim().toLowerCase();
    if (['css', 'preparing', 'sss', 'unloaded', 'match', 'results', 'prize', 'title',
      'main', 'opening', 'opening_vs', 'unknown'].includes(text)) {
      const code = Object.entries(SCENE_NAMES).find(([, name]) => name === text)?.[0];
      return {name: text, code: code === undefined ? 0 : Number(code), malformed: false};
    }
    return {name: 'unknown', code: 0, malformed: true};
  }
  const code = integer(value);
  if (code !== null && code >= 0 && code <= 255) {
    return {name: SCENE_NAMES[code] || 'unknown', code, malformed: false};
  }
  return {name: 'unknown', code: 0, malformed: value !== undefined && value !== null};
}

// Native uses a numeric scene identity.  Keeping this helper numeric avoids
// allocating a normalization object on every callback.
function nativeSceneCode(value) {
  if (typeof value === 'string') {
    const text = value.trim().toLowerCase();
    if (text === 'css') return 1;
    if (text === 'preparing') return 2;
    if (text === 'sss') return 3;
    if (text === 'unloaded') return 6;
    if (text === 'match' || text === 'gameplay') return 7;
    if (text === 'results') return 8;
    if (text === 'prize') return 9;
    if (text === 'title') return 10;
    if (text === 'main') return 11;
    if (text === 'opening') return 12;
    if (text === 'opening_vs') return 13;
    return 0;
  }
  const code = integer(value);
  return code !== null && code >= 0 && code <= 255 ? code : 0;
}

function normalizeLifecycle(value) {
  const name = typeof value === 'string' ? value.trim().toLowerCase() : 'unknown';
  return LIFECYCLE_NAMES.has(name)
    ? {name, malformed: false}
    : {name: 'unknown', malformed: true};
}

function normalizeAudioState(value) {
  if (typeof value !== 'string') return 'unknown';
  const text = value.trim().toLowerCase();
  return ['running', 'suspended', 'closed', 'interrupted', 'unknown'].includes(text) ? text : 'unknown';
}

function safeErrorKind(error) {
  const name = typeof error?.name === 'string' ? error.name : '';
  if (/quota/i.test(name) || error?.code === 22 || error?.code === 1014) return 'quota';
  if (/security|denied|notallowed|permission/i.test(name) || error?.code === 18) return 'denied';
  if (/version|abort|invalidstate/i.test(name)) return 'failed';
  return 'failed';
}

function promiseResult(value) {
  try {
    return Promise.resolve(value);
  } catch (error) {
    return Promise.reject(error);
  }
}

function copyNumber(value, malformedState, fallback = 0) {
  const result = finite(value);
  if (result === null) {
    if (value !== undefined && value !== null) malformedState.value = true;
    return fallback;
  }
  return result;
}

function utf8Size(value) {
  let encoded;
  try { encoded = JSON.stringify(value); } catch { return null; }
  if (typeof encoded !== 'string') return null;
  try {
    return typeof TextEncoder === 'function'
      ? new TextEncoder().encode(encoded).byteLength
      : unescape(encodeURIComponent(encoded)).length;
  } catch {
    return null;
  }
}

const isNullableFinite = value => value === null || finite(value) !== null;
const isNullableNonNegative = value => value === null || (finite(value) !== null && value >= 0);
const isNullableInteger = value => value === null || integer(value) !== null;
const isNullableNonNegativeInteger = value => value === null || (integer(value) !== null && value >= 0);
const isBoolean = value => value === true || value === false;
const isNullableBoolean = value => value === null || isBoolean(value);

function strictString(value, maximum = 128) {
  return typeof value === 'string' && value.length > 0 && value.length <= maximum;
}

function strictStoredNative(native) {
  const keys = ['callback_count', 'first_timestamp', 'last_timestamp', 'max_update_ms',
    'max_draw_ms', 'max_total_ms', 'max_preparation_ms', 'max_debt_ticks', 'max_interval_ms',
    'min_interval_ms', 'interval_count', 'interval_sum_ms', 'malformed_count', 'unavailable_count'];
  return strictStoredKeys(native, keys) &&
    isNullableFinite(native.first_timestamp) && isNullableFinite(native.last_timestamp) &&
    isNullableNonNegative(native.max_update_ms) && isNullableNonNegative(native.max_draw_ms) &&
    isNullableNonNegative(native.max_total_ms) && isNullableNonNegative(native.max_preparation_ms) &&
    isNullableNonNegative(native.max_debt_ticks) && isNullableNonNegative(native.max_interval_ms) &&
    isNullableNonNegative(native.min_interval_ms) && isNullableNonNegative(native.interval_sum_ms) &&
    [native.callback_count, native.interval_count, native.malformed_count, native.unavailable_count]
      .every(value => Number.isSafeInteger(value) && value >= 0);
}

function strictStoredHistory(history) {
  if (!strictStoredKeys(history,
    ['seconds', 'hz', 'sample_interval_ms', 'columns', 'rows', 'evicted', 'evicted_count', 'truncated'])) return false;
  return finite(history.seconds) !== null && history.seconds >= 0 &&
    Number.isSafeInteger(history.hz) && history.hz > 0 &&
    isNullableNonNegative(history.sample_interval_ms) && isBoolean(history.evicted) &&
    Number.isSafeInteger(history.evicted_count) && history.evicted_count >= 0 &&
    isBoolean(history.truncated) && strictStoredRows(history.rows, history.columns);
}

function strictStoredCapabilities(capabilities) {
  if (!strictStoredKeys(capabilities, ['native', 'audio', 'longtask'])) return false;
  for (const key of ['native', 'audio', 'longtask']) {
    const value = capabilities[key];
    if (!strictStoredKeys(value, ['available', 'observed', 'reason']) ||
        !isBoolean(value.available) || !isBoolean(value.observed) ||
        !(value.reason === null || ['not_observed', 'unavailable', 'supported_no_events'].includes(value.reason))) return false;
  }
  return true;
}

function strictStoredAudio(audio) {
  const keys = ['callback_count', 'first_timestamp', 'last_timestamp', 'max_callback_ms',
    'max_interval_ms', 'max_queue_depth', 'underruns', 'overflows', 'clock_seconds',
    'context_state', 'enabled', 'malformed_count'];
  return strictStoredKeys(audio, keys) && Number.isSafeInteger(audio.callback_count) && audio.callback_count >= 0 &&
    isNullableFinite(audio.first_timestamp) && isNullableFinite(audio.last_timestamp) &&
    isNullableNonNegative(audio.max_callback_ms) && isNullableNonNegative(audio.max_interval_ms) &&
    isNullableNonNegative(audio.max_queue_depth) && isNullableNonNegativeInteger(audio.underruns) &&
    isNullableNonNegativeInteger(audio.overflows) && isNullableNonNegative(audio.clock_seconds) &&
    ['running', 'suspended', 'closed', 'interrupted', 'unknown'].includes(audio.context_state) &&
    isNullableBoolean(audio.enabled) && Number.isSafeInteger(audio.malformed_count) && audio.malformed_count >= 0;
}

function strictStoredClient(client) {
  return strictStoredKeys(client, ['platform', 'browser', 'browser_major']) &&
    ['unknown', 'macos', 'ios', 'windows', 'linux', 'android'].includes(client.platform) &&
    ['unknown', 'edge', 'opera', 'chrome', 'firefox', 'safari'].includes(client.browser) &&
    (client.browser_major === null || (Number.isSafeInteger(client.browser_major) && client.browser_major >= 0 && client.browser_major <= 1000));
}

function strictStoredFlags(flags) {
  const keys = ['history_evicted', 'history_evicted_count', 'incident_evicted', 'incident_evicted_count',
    'storage_evicted', 'storage_evicted_count', 'report_truncated', 'persistence_failed',
    'persistence_unavailable', 'storage_denied', 'quota_exceeded', 'malformed_record', 'malformed_count',
    'recent_event_evicted', 'recent_event_evicted_count', 'recovery_timed_out', 'clock_regression_count',
    'crypto_unavailable'];
  const countKeys = new Set(['history_evicted_count', 'incident_evicted_count', 'storage_evicted_count',
    'malformed_count', 'recent_event_evicted_count', 'clock_regression_count']);
  return strictStoredKeys(flags, keys) && Object.entries(flags).every(([key, value]) =>
    countKeys.has(key) ? Number.isSafeInteger(value) && value >= 0 : isBoolean(value));
}

function strictStoredLimits(limits) {
  const keys = ['history_seconds', 'history_hz', 'history_capacity', 'post_event_cap', 'recent_event_cap',
    'max_incidents', 'max_report_bytes', 'max_storage_bytes'];
  return strictStoredKeys(limits, keys) && finite(limits.history_seconds) !== null && limits.history_seconds >= 0 && limits.history_seconds <= 60 &&
    Number.isSafeInteger(limits.history_hz) && limits.history_hz > 0 && limits.history_hz <= 10 &&
    Number.isSafeInteger(limits.history_capacity) && limits.history_capacity > 0 && limits.history_capacity <= 600 &&
    Number.isSafeInteger(limits.post_event_cap) && limits.post_event_cap > 0 && limits.post_event_cap <= 128 &&
    Number.isSafeInteger(limits.recent_event_cap) && limits.recent_event_cap > 0 && limits.recent_event_cap <= 32 &&
    Number.isSafeInteger(limits.max_incidents) && limits.max_incidents > 0 && limits.max_incidents <= 4 &&
    Number.isSafeInteger(limits.max_report_bytes) && limits.max_report_bytes >= 16 * 1024 && limits.max_report_bytes <= DEFAULT_MAX_REPORT_BYTES &&
    Number.isSafeInteger(limits.max_storage_bytes) && limits.max_storage_bytes > 0 && limits.max_storage_bytes <= DEFAULT_MAX_STORAGE_BYTES;
}

function normalizeStoredIncident(value) {
  try {
    if (!value || typeof value !== 'object') return null;
    if (!strictStoredKeys(value, ['id', 'schema', 'version', 'session_id', 'identity', 'environment',
      'client', 'capabilities', 'audio', 'flags', 'limits', 'incident'])) return null;
    const incident = value.incident;
    const identity = value.identity;
    const environment = value.environment;
    if (!incident || typeof incident !== 'object' || typeof incident.id !== 'string' || incident.id.length > 64 ||
        !Array.isArray(incident.pre_events) || incident.pre_events.length > 32 ||
        !Array.isArray(incident.post_events) || incident.post_events.length > 128 ||
        !incident.history || typeof incident.history !== 'object' ||
        !Array.isArray(incident.history.rows) || incident.history.rows.length > 600) return null;
    const size = utf8Size(value);
    if (size === null || size > MAX_STORED_RECORD_BYTES) return null;
    if (!strictStoredKeys(value, ['id', 'schema', 'version', 'session_id', 'identity', 'environment',
      'client', 'capabilities', 'audio', 'flags', 'limits', 'incident']) ||
        value.schema !== RUNTIME_DIAGNOSTICS_SCHEMA || value.version !== RUNTIME_DIAGNOSTICS_VERSION ||
        typeof value.session_id !== 'string' || value.session_id.length > 96 || !/^session-[a-z0-9-]{16,96}$/.test(value.session_id) ||
        typeof value.id !== 'string' || value.id.length > 160 ||
        value.id !== `${value.session_id}:${incident?.id ?? ''}` ||
        !strictStoredKeys(identity, ['schema_version', 'source_commit', 'runtime_hash', 'build_profile']) ||
        identity.schema_version !== 1 ||
        !(identity.source_commit === null || (typeof identity.source_commit === 'string' && /^[a-f0-9]{40}$/.test(identity.source_commit))) ||
        !(identity.runtime_hash === null || (typeof identity.runtime_hash === 'string' && /^[a-f0-9]{16}$/.test(identity.runtime_hash))) ||
        !(typeof identity.build_profile === 'string' && PROFILE_RE.test(identity.build_profile)) ||
        !strictStoredKeys(environment, ['kind', 'host', 'origin', 'secure']) ||
        !['production', 'staging', 'local', 'unknown'].includes(environment.kind) ||
        !(environment.host === null || strictString(environment.host, 253)) ||
        !(environment.origin === null || strictString(environment.origin, 2048)) || !isBoolean(environment.secure) ||
        !strictStoredClient(value.client) || !strictStoredCapabilities(value.capabilities) ||
        !strictStoredAudio(value.audio) || !strictStoredFlags(value.flags) || !strictStoredLimits(value.limits) ||
        !incident || typeof incident !== 'object' ||
        !strictStoredKeys(incident, ['id', 'timestamp', 'captured_at_ms', 'reason', 'reason_code', 'value', 'threshold',
          'source_frame', 'scene', 'scene_code', 'clock_owner', 'clock_owner_code', 'native', 'history',
          'pre_events', 'post_events', 'post_truncated', 'post_truncated_count', 'closed', 'recovery',
          'recovery_timed_out']) || !/^incident-[0-9]+$/.test(incident.id) ||
        !isNullableFinite(incident.timestamp) || !Number.isSafeInteger(incident.captured_at_ms) || incident.captured_at_ms < 0 ||
        !['simulation_debt', 'audio_debt', 'nonfinite_clock', 'runtime_failure', 'clock_regression', 'unknown'].includes(incident.reason) ||
        !Number.isSafeInteger(incident.reason_code) || incident.reason_code < 0 ||
        !isNullableFinite(incident.value) || !isNullableFinite(incident.threshold) ||
        !isNullableNonNegativeInteger(incident.source_frame) ||
        !['unknown', 'css', 'preparing', 'sss', 'unloaded', 'match', 'results', 'prize', 'title', 'main', 'opening', 'opening_vs'].includes(incident.scene) ||
        !Number.isSafeInteger(incident.scene_code) || incident.scene_code < 0 || incident.scene_code > 255 ||
        !['other', 'simulation', 'audio'].includes(incident.clock_owner) ||
        !Number.isSafeInteger(incident.clock_owner_code) || incident.clock_owner_code < 0 || incident.clock_owner_code > 2 ||
        !strictStoredNative(incident.native) || !strictStoredHistory(incident.history) ||
        !strictStoredEvents(incident.pre_events) || !strictStoredEvents(incident.post_events) ||
        incident.pre_events.length > 32 || incident.post_events.length > 128 ||
        !isBoolean(incident.post_truncated) || !Number.isSafeInteger(incident.post_truncated_count) || incident.post_truncated_count < 0 ||
        !isBoolean(incident.closed) || !(incident.recovery === null || ['recovered', 'recovery', 'resume', 'timeout'].includes(incident.recovery)) ||
        !isBoolean(incident.recovery_timed_out)) return null;
    const reasonInfo = normalizeReason(incident.reason);
    const sceneInfo = normalizeScene(incident.scene_code);
    const ownerInfo = normalizeClockOwner(incident.clock_owner_code);
    if (reasonInfo.name !== incident.reason || reasonInfo.code !== incident.reason_code ||
        sceneInfo.name !== incident.scene || sceneInfo.code !== incident.scene_code ||
        ownerInfo.name !== incident.clock_owner || ownerInfo.code !== incident.clock_owner_code) return null;
    const safeIdentity = normalizeIdentity(value.identity);
    if (safeIdentity.source_commit !== value.identity.source_commit ||
        safeIdentity.runtime_hash !== value.identity.runtime_hash ||
        safeIdentity.build_profile !== value.identity.build_profile) return null;
    const safeEnvironment = hostEnvironment(value.environment.origin);
    if (safeEnvironment.kind !== value.environment.kind || safeEnvironment.host !== value.environment.host ||
        safeEnvironment.origin !== value.environment.origin || safeEnvironment.secure !== value.environment.secure) return null;
    return clone(value);
  } catch {
    return null;
  }
}

function strictStoredKeys(value, allowed) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return false;
  const keys = new Set(allowed);
  return Object.keys(value).length === keys.size && Object.keys(value).every(key => keys.has(key));
}

function strictStoredEvents(events) {
  if (!Array.isArray(events) || events.length > 128) return false;
  for (const event of events) {
    if (!event || typeof event !== 'object' || typeof event.type !== 'string') return false;
    const allowed = event.type === 'native'
      ? ['type', 'timestamp', 'source_frame', 'total_ms', 'interval_ms']
      : event.type === 'lifecycle'
        ? ['type', 'kind', 'timestamp', 'source_frame', 'scene', 'scene_code', 'duration_ms', 'bytes', 'files']
        : event.type === 'longtask'
          ? ['type', 'timestamp', 'duration_ms', 'source_frame']
          : event.type === 'audio'
            ? ['type', 'timestamp', 'callback_ms', 'interval_ms', 'queue_depth', 'underruns',
              'overflows', 'clock_seconds', 'context_state', 'enabled', 'source_frame']
            : null;
    if (!allowed || !strictStoredKeys(event, allowed)) return false;
    if (!isNullableFinite(event.timestamp) || !isNullableNonNegativeInteger(event.source_frame)) return false;
    if (event.type === 'native' && (!isNullableNonNegative(event.total_ms) || !isNullableNonNegative(event.interval_ms))) return false;
    if (event.type === 'lifecycle' &&
        (!LIFECYCLE_NAMES.has(event.kind) || !['unknown', 'css', 'preparing', 'sss', 'unloaded', 'match', 'results', 'prize', 'title', 'main', 'opening', 'opening_vs'].includes(event.scene) ||
          !Number.isSafeInteger(event.scene_code) || event.scene_code < 0 || event.scene_code > 255 ||
          normalizeScene(event.scene_code).name !== event.scene ||
          !isNullableNonNegative(event.duration_ms) || !isNullableNonNegative(event.bytes) || !isNullableNonNegative(event.files))) return false;
    if (event.type === 'longtask' && !isNullableNonNegative(event.duration_ms)) return false;
    if (event.type === 'audio' &&
        (!isNullableNonNegative(event.callback_ms) || !isNullableNonNegative(event.interval_ms) ||
          !isNullableNonNegative(event.queue_depth) || !isNullableNonNegativeInteger(event.underruns) ||
          !isNullableNonNegativeInteger(event.overflows) || !isNullableNonNegative(event.clock_seconds) ||
          !['running', 'suspended', 'closed', 'interrupted', 'unknown'].includes(event.context_state) ||
          !isNullableBoolean(event.enabled))) return false;
  }
  return true;
}

function strictStoredRows(rows, columns) {
  if (Array.isArray(columns) && columns.length === 0 && Array.isArray(rows) && rows.length === 0) return true;
  if (!Array.isArray(columns) || columns.length !== HISTORY_COLUMN_COUNT ||
      columns.some((column, index) => column !== HISTORY_COLUMNS[index]) || !Array.isArray(rows) || rows.length > 600) return false;
  return rows.every(row => Array.isArray(row) && row.length === HISTORY_COLUMN_COUNT &&
    row.every(value => value === null || finite(value) !== null));
}

/**
 * Create a bounded local incident recorder.
 *
 * The native positional signature is intentionally long so a callback can
 * write directly into the numeric ring without constructing an observation
 * object:
 *
 * observeNative(timestamp, sourceFrame, scene, stage, fighter0, fighter1,
 *   fighter2, fighter3, debtTicks, updateMs, drawMs, totalMs, preparationMs,
 *   queuedDelta, createdDelta, textureUploadBytes, sourceSteps, sourceDraws,
 *   running)
 *
 * An object form is also accepted for tests and low-frequency callers.  The
 * returned value is a boolean sampled flag; callers do not need to allocate a
 * result object on the callback path.
 */
export function createRuntimeDiagnostics(options = {}) {
  const root = options.globalThis || globalThis;
  const perf = options.performance || root.performance || null;
  const now = typeof options.now === 'function'
    ? options.now
    : () => finite(perf?.now?.()) ?? 0;
  const wallNow = typeof options.wallNow === 'function' ? options.wallNow : () => Date.now();
  const identity = normalizeIdentity(options.identity);
  const environment = hostEnvironment(options.origin ?? root.location?.origin);
  const historySeconds = clampNonNegative(options.historySeconds, DEFAULT_HISTORY_SECONDS, 60);
  const historyHz = clampPositiveInteger(options.historyHz, DEFAULT_HISTORY_HZ, 10);
  const historyCapacity = Math.max(1, Math.min(600, Math.ceil(historySeconds * historyHz)));
  const sampleIntervalMs = 1000 / historyHz;
  const maxIncidents = clampPositiveInteger(options.maxIncidents, DEFAULT_MAX_INCIDENTS, 4);
  const maxReportBytes = Math.max(16 * 1024,
    clampPositiveInteger(options.maxReportBytes, DEFAULT_MAX_REPORT_BYTES, DEFAULT_MAX_REPORT_BYTES));
  const maxStorageBytes = clampPositiveInteger(options.maxStorageBytes, DEFAULT_MAX_STORAGE_BYTES, DEFAULT_MAX_STORAGE_BYTES);
  const postEventCap = clampPositiveInteger(options.postEventCap, DEFAULT_POST_EVENT_CAP, 128);
  const recentEventCap = clampPositiveInteger(options.recentEventCap, DEFAULT_RECENT_EVENT_CAP, DEFAULT_RECENT_EVENT_CAP);
  const postWindowMs = clampNonNegative(options.postWindowMs, DEFAULT_POST_WINDOW_MS, 60000);
  let persistenceUnavailable = false;
  let storageDenied = false;
  let cryptoUnavailable = false;
  let indexedDB = null;
  let storage = null;
  try { indexedDB = options.indexedDB ?? root.indexedDB ?? null; }
  catch { persistenceUnavailable = true; storageDenied = true; }
  try { storage = options.storage ?? null; }
  catch { persistenceUnavailable = true; storageDenied = true; }
  const nativeConfiguredUnavailable = options.nativeAvailable === false;
  const audioConfiguredUnavailable = options.audioAvailable === false;
  const capabilities = {
    native: {available: false, observed: false, reason: 'not_observed'},
    audio: {available: false, observed: false, reason: 'not_observed'},
    longtask: {available: false, observed: false, reason: 'unavailable'},
  };
  if (options.nativeAvailable === false) capabilities.native.reason = 'unavailable';
  if (options.audioAvailable === false) capabilities.audio.reason = 'unavailable';
  const observer = options.PerformanceObserver ?? root.PerformanceObserver;
  const observerTypes = Array.isArray(observer?.supportedEntryTypes)
    ? observer.supportedEntryTypes : [];
  capabilities.longtask.available = options.longtaskAvailable !== false &&
    (options.longtaskAvailable === true || observerTypes.includes('longtask'));
  capabilities.longtask.reason = capabilities.longtask.available ? 'supported_no_events' : 'unavailable';

  const history = new Float64Array(historyCapacity * HISTORY_COLUMN_COUNT);
  const nativeScratch = new Float64Array(HISTORY_COLUMN_COUNT);
  let historyHead = 0;
  let historyCount = 0;
  let historyEvicted = false;
  let historyEvictedCount = 0;
  let lastSampleTimestamp = null;
  let lastObservationTimestamp = null;
  let callbackCount = 0;
  let malformedCount = 0;
  let nativeUnavailableCount = 0;
  let maxUpdateMs = null;
  let maxDrawMs = null;
  let maxTotalMs = null;
  let maxPreparationMs = null;
  let maxDebtTicks = null;
  let maxIntervalMs = null;
  let minIntervalMs = null;
  let sumIntervalMs = null;
  let intervalCount = 0;
  let windowMaxDebtTicks = null;
  let windowMaxUpdateMs = null;
  let windowMaxDrawMs = null;
  let windowMaxTotalMs = null;
  let windowMaxPreparationMs = null;
  let windowMaxIntervalMs = null;
  let windowQueuedDelta = null;
  let windowCreatedDelta = null;
  let windowTextureUploadBytes = null;
  let windowSourceSteps = null;
  let windowSourceDraws = null;
  let firstObservationTimestamp = null;
  let active = options.active !== false;
  let paused = false;
  let incidentSequence = 0;
  let incidents = [];
  let incidentEvicted = false;
  let incidentEvictedCount = 0;
  let storageEvicted = false;
  let storageEvictedCount = 0;
  let reportTruncated = false;
  let persistenceFailed = false;
  let quotaExceeded = false;
  let malformedRecord = false;
  let persistPromise = null;
  let pausePersistencePromise = null;
  const schedule = typeof root.setTimeout === 'function'
    ? root.setTimeout.bind(root) : (callback, delay) => setTimeout(callback, delay);
  const cancel = typeof root.clearTimeout === 'function'
    ? root.clearTimeout.bind(root) : (timer) => clearTimeout(timer);
  let persistedCount = 0;
  let inactiveGeneration = 0;
  let lifecycleCount = 0;
  let longtaskCount = 0;
  let audioCount = 0;
  let lifecycleMalformedCount = 0;
  let audioMalformedCount = 0;
  let currentSceneCode = 0;
  let clockRegressionCount = 0;
  let recentEvents = [];
  let recentEventEvicted = false;
  let recentEventEvictedCount = 0;
  const session = ephemeralSessionId(root);
  const sessionId = session.id;
  cryptoUnavailable = session.fallback;
  const client = coarseClient(root);

  const audioAggregate = {
    maxCallbackMs: null, maxIntervalMs: null, maxQueueDepth: null,
    underruns: null, overflows: null, clockSeconds: null, contextState: 'unknown', enabled: null,
    lastTimestamp: null, firstTimestamp: null,
  };

  function markMalformed() {
    malformedCount++;
    malformedRecord = true;
  }

  function appendRecentEvent(event) {
    if (recentEvents.length >= recentEventCap) {
      recentEvents.shift();
      recentEventEvicted = true;
      recentEventEvictedCount++;
    }
    recentEvents.push(event);
  }

  function writeHistoryRow(values) {
    const row = historyHead * HISTORY_COLUMN_COUNT;
    for (let i = 0; i < HISTORY_COLUMN_COUNT; i++) history[row + i] = values[i];
    historyHead = (historyHead + 1) % historyCapacity;
    if (historyCount < historyCapacity) historyCount++;
    else {
      historyEvicted = true;
      historyEvictedCount++;
    }
  }

  function historyRows() {
    const rows = [];
    const start = historyCount === historyCapacity ? historyHead : 0;
    for (let n = 0; n < historyCount; n++) {
      const source = ((start + n) % historyCapacity) * HISTORY_COLUMN_COUNT;
      const row = [];
      for (let i = 0; i < HISTORY_COLUMN_COUNT; i++) {
        const value = history[source + i];
        row.push(Number.isFinite(value) ? value : null);
      }
      if (row[HISTORY_INDEX.source_frame] === -1) row[HISTORY_INDEX.source_frame] = null;
      rows.push(row);
    }
    return rows;
  }

  function nativeSummary() {
    return {
      callback_count: callbackCount,
      first_timestamp: firstObservationTimestamp,
      last_timestamp: lastObservationTimestamp,
      max_update_ms: maxUpdateMs,
      max_draw_ms: maxDrawMs,
      max_total_ms: maxTotalMs,
      max_preparation_ms: maxPreparationMs,
      max_debt_ticks: maxDebtTicks,
      max_interval_ms: maxIntervalMs,
      min_interval_ms: minIntervalMs,
      interval_count: intervalCount,
      interval_sum_ms: sumIntervalMs,
      malformed_count: malformedCount,
      unavailable_count: nativeUnavailableCount,
    };
  }

  function currentHistory() {
    return {
      seconds: historySeconds,
      hz: historyHz,
      sample_interval_ms: sampleIntervalMs,
      columns: HISTORY_COLUMNS,
      rows: historyRows(),
      evicted: historyEvicted,
      evicted_count: historyEvictedCount,
      truncated: false,
    };
  }

  function appendPost(incident, event) {
    if (incident.closed || incident.post_events.length >= postEventCap) {
      incident.post_truncated = true;
      incident.post_truncated_count++;
      return false;
    }
    incident.post_events.push(event);
    return true;
  }

  function maybeCloseByTime(timestamp) {
    if (timestamp === null) return;
    for (const incident of incidents) {
      if (!incident.closed && postWindowMs > 0 && timestamp - incident.timestamp >= postWindowMs) {
        incident.closed = true;
        incident.recovery = 'timeout';
        incident.recovery_timed_out = true;
        if (incident.recoveryTimer !== null) {
          cancel(incident.recoveryTimer);
          incident.recoveryTimer = null;
        }
        if (!active) void persist();
      }
    }
  }

  function appendNativePost(timestamp, sourceFrame, totalMs, intervalMs) {
    if (!incidents.length) return;
    for (const incident of incidents) {
      if (incident.closed) continue;
      // One compact sample per bounded interval is enough for post-trigger
      // context.  The full pre-trigger history remains in the typed ring.
      if (incident.post_sample_count > 0 && incident.last_post_sample !== null && timestamp !== null &&
          timestamp - incident.last_post_sample < sampleIntervalMs) continue;
      incident.last_post_sample = timestamp;
      incident.post_sample_count++;
      appendPost(incident, {
        type: 'native',
        timestamp,
        source_frame: sourceFrame === -1 ? null : sourceFrame,
        total_ms: totalMs,
        interval_ms: intervalMs,
      });
    }
  }

  function snapshotIncident(reason, value, threshold, sourceFrame, scene, clockOwner) {
    const nowValue = finite(now()) ?? 0;
    const reasonInfo = normalizeReason(reason);
    const ownerInfo = normalizeClockOwner(clockOwner);
    const valueNumber = finite(value);
    const thresholdNumber = finite(threshold);
    const sourceFrameNumber = integer(sourceFrame);
    const sceneInfo = normalizeScene(scene === undefined || scene === null ? currentSceneCode : scene);
    const nonfiniteClockValue = reasonInfo.code === 3;
    if (reasonInfo.malformed || ownerInfo.malformed || sceneInfo.malformed ||
        (value !== undefined && value !== null && valueNumber === null && !nonfiniteClockValue) ||
        (threshold !== undefined && threshold !== null && thresholdNumber === null && !nonfiniteClockValue) ||
        (sourceFrame !== undefined && sourceFrame !== null && sourceFrameNumber === null)) markMalformed();
    if (reasonInfo.code === 8) clockRegressionCount++;
    currentSceneCode = sceneInfo.code;
    const wallCapture = finite(wallNow());
    const incident = {
      id: `incident-${++incidentSequence}`,
      timestamp: nowValue,
      captured_at_ms: Number.isSafeInteger(wallCapture) && wallCapture >= 0 ? wallCapture :
        Number.isSafeInteger(Date.now()) && Date.now() >= 0 ? Date.now() : 0,
      reason: reasonInfo.name,
      reason_code: reasonInfo.code,
      value: valueNumber,
      threshold: thresholdNumber,
      source_frame: sourceFrameNumber === -1 ? null : sourceFrameNumber,
      scene: sceneInfo.name,
      scene_code: sceneInfo.code,
      clock_owner: ownerInfo.name,
      clock_owner_code: ownerInfo.code,
      native: nativeSummary(),
      history: currentHistory(),
      pre_events: recentEvents.slice(),
      post_events: [],
      post_truncated: false,
      post_truncated_count: 0,
      last_post_sample: nowValue,
      post_sample_count: 0,
      closed: false,
      recovery: null,
      recovery_timed_out: false,
      recoveryTimer: null,
    };
    // A trigger is an explicit source boundary.  The trigger snapshot is
    // immutable to callers; only its bounded post-event fields evolve inside
    // this private record until export.
    if (incidents.length >= maxIncidents) {
      const evicted = incidents.shift();
      if (evicted.recoveryTimer !== null) cancel(evicted.recoveryTimer);
      incidentEvicted = true;
      incidentEvictedCount++;
    }
    incidents.push(incident);
    if (postWindowMs > 0) {
      incident.recoveryTimer = schedule(() => {
        if (!incident.closed) {
          incident.closed = true;
          incident.recovery = 'timeout';
          incident.recovery_timed_out = true;
        }
        incident.recoveryTimer = null;
        if (!active) void persist();
      }, postWindowMs);
      if (typeof incident.recoveryTimer?.unref === 'function') incident.recoveryTimer.unref();
    }
    return incident.id;
  }

  function observeNativeObject(observation = {}) {
    return observeNative(
      observation.timestamp, observation.sourceFrame ?? observation.source_frame,
      observation.scene, observation.stage, observation.fighter0, observation.fighter1,
      observation.fighter2, observation.fighter3, observation.debtTicks ?? observation.debt_ticks,
      observation.updateMs ?? observation.update_ms, observation.drawMs ?? observation.draw_ms,
      observation.totalMs ?? observation.total_ms, observation.preparationMs ?? observation.preparation_ms,
      observation.queuedDelta ?? observation.queued_delta, observation.createdDelta ?? observation.created_delta,
      observation.textureUploadBytes ?? observation.texture_upload_bytes,
      observation.sourceSteps ?? observation.source_steps, observation.sourceDraws ?? observation.source_draws,
      observation.running,
    );
  }

  function observeNative(
    timestamp, sourceFrame, scene, stage, fighter0, fighter1, fighter2, fighter3,
    debtTicks, updateMs, drawMs, totalMs, preparationMs, queuedDelta, createdDelta,
    textureUploadBytes, sourceSteps, sourceDraws, running,
  ) {
    if (timestamp && typeof timestamp === 'object') return observeNativeObject(timestamp);
    if (!active && !paused && !incidents.length) return false;
    if (nativeConfiguredUnavailable) {
      nativeUnavailableCount++;
      return false;
    }
    capabilities.native.available = true;
    capabilities.native.observed = true;
    capabilities.native.reason = null;
    const at = finite(timestamp);
    const observedAt = at ?? (finite(now()) ?? 0);
    let malformed = at === null && timestamp !== undefined && timestamp !== null;
    const sourceRaw = integer(sourceFrame);
    if (sourceRaw === null && sourceFrame !== undefined && sourceFrame !== null) malformed = true;
    const source = sourceRaw === -1 ? null : sourceRaw;
    const sceneCode = scene === undefined || scene === null ? null : scene === -1 ? 0 : nativeSceneCode(scene);
    if (scene !== undefined && scene !== null && sceneCode === 0 && scene !== 0 && scene !== -1) malformed = true;
    const runningValue = running === undefined || running === null ? null :
      running === true || running === 1 ? true : running === false || running === 0 ? false : null;
    if (running !== undefined && running !== null && runningValue === null) malformed = true;
    if (invalidNativeNumber(stage, true) || invalidNativeNumber(fighter0, true) ||
        invalidNativeNumber(fighter1, true) || invalidNativeNumber(fighter2, true) ||
        invalidNativeNumber(fighter3, true) || invalidNativeNumber(debtTicks) ||
        invalidNativeNumber(updateMs) || invalidNativeNumber(drawMs) ||
        invalidNativeNumber(totalMs) || invalidNativeNumber(preparationMs) ||
        invalidNativeNumber(queuedDelta) || invalidNativeNumber(createdDelta) ||
        invalidNativeNumber(textureUploadBytes) || invalidNativeNumber(sourceSteps, true) ||
        invalidNativeNumber(sourceDraws, true)) malformed = true;
    const stageValue = nativeNumber(stage, true);
    const fighter0Value = nativeNumber(fighter0, true);
    const fighter1Value = nativeNumber(fighter1, true);
    const fighter2Value = nativeNumber(fighter2, true);
    const fighter3Value = nativeNumber(fighter3, true);
    const debtValue = nativeNumber(debtTicks);
    const updateValue = nativeNumber(updateMs);
    const drawValue = nativeNumber(drawMs);
    const totalValue = nativeNumber(totalMs);
    const preparationValue = nativeNumber(preparationMs);
    const queuedValue = nativeNumber(queuedDelta);
    const createdValue = nativeNumber(createdDelta);
    const textureValue = nativeNumber(textureUploadBytes);
    const stepsValue = nativeNumber(sourceSteps, true);
    const drawsValue = nativeNumber(sourceDraws, true);
    const stageNumber = stageValue;
    const fighter0Number = fighter0Value;
    const fighter1Number = fighter1Value;
    const fighter2Number = fighter2Value;
    const fighter3Number = fighter3Value;
    const debtNumber = debtValue;
    const updateNumber = updateValue;
    const drawNumber = drawValue;
    const totalNumber = totalValue;
    const preparationNumber = preparationValue;
    const queuedNumber = queuedValue;
    const createdNumber = createdValue;
    const textureNumber = textureValue;
    const stepsNumber = stepsValue;
    const drawsNumber = drawsValue;
    const previous = lastObservationTimestamp;
    let intervalMs = null;
    if (previous !== null) {
      intervalMs = observedAt - previous;
      if (!(intervalMs >= 0) || !Number.isFinite(intervalMs)) {
        clockRegressionCount++;
        intervalMs = null;
      }
      if (intervalMs !== null) {
        maxIntervalMs = maxNullable(maxIntervalMs, intervalMs);
        minIntervalMs = minIntervalMs === null ? intervalMs : Math.min(minIntervalMs, intervalMs);
        sumIntervalMs = sumNullable(sumIntervalMs, intervalMs);
        intervalCount++;
      }
    }
    windowMaxIntervalMs = maxNullable(windowMaxIntervalMs, intervalMs);
    lastObservationTimestamp = observedAt;
    if (firstObservationTimestamp === null) firstObservationTimestamp = observedAt;
    callbackCount++;
    maxUpdateMs = maxNullable(maxUpdateMs, updateNumber);
    maxDrawMs = maxNullable(maxDrawMs, drawNumber);
    maxTotalMs = maxNullable(maxTotalMs, totalNumber);
    maxPreparationMs = maxNullable(maxPreparationMs, preparationNumber);
    maxDebtTicks = maxNullable(maxDebtTicks, debtNumber);
    windowMaxUpdateMs = maxNullable(windowMaxUpdateMs, updateNumber);
    windowMaxDrawMs = maxNullable(windowMaxDrawMs, drawNumber);
    windowMaxTotalMs = maxNullable(windowMaxTotalMs, totalNumber);
    windowMaxPreparationMs = maxNullable(windowMaxPreparationMs, preparationNumber);
    windowMaxDebtTicks = maxNullable(windowMaxDebtTicks, debtNumber);
    windowQueuedDelta = sumNullable(windowQueuedDelta, queuedNumber);
    windowCreatedDelta = sumNullable(windowCreatedDelta, createdNumber);
    windowTextureUploadBytes = sumNullable(windowTextureUploadBytes, textureNumber);
    windowSourceSteps = sumNullable(windowSourceSteps, stepsNumber);
    windowSourceDraws = sumNullable(windowSourceDraws, drawsNumber);
    if (source === null) nativeUnavailableCount++;
    if (sceneCode !== null) currentSceneCode = sceneCode;
    if (malformed) markMalformed();
    const shouldSample = lastSampleTimestamp === null || observedAt - lastSampleTimestamp >= sampleIntervalMs;
    if (shouldSample) {
      nativeScratch[HISTORY_INDEX.timestamp] = observedAt;
      nativeScratch[HISTORY_INDEX.source_frame] = source === null ? -1 : source;
      nativeScratch[HISTORY_INDEX.scene] = scratchValue(sceneCode);
      nativeScratch[HISTORY_INDEX.stage] = scratchValue(stageNumber);
      nativeScratch[HISTORY_INDEX.fighter0] = scratchValue(fighter0Number);
      nativeScratch[HISTORY_INDEX.fighter1] = scratchValue(fighter1Number);
      nativeScratch[HISTORY_INDEX.fighter2] = scratchValue(fighter2Number);
      nativeScratch[HISTORY_INDEX.fighter3] = scratchValue(fighter3Number);
      nativeScratch[HISTORY_INDEX.debt_ticks] = scratchValue(windowMaxDebtTicks);
      nativeScratch[HISTORY_INDEX.update_ms] = scratchValue(windowMaxUpdateMs);
      nativeScratch[HISTORY_INDEX.draw_ms] = scratchValue(windowMaxDrawMs);
      nativeScratch[HISTORY_INDEX.total_ms] = scratchValue(windowMaxTotalMs);
      nativeScratch[HISTORY_INDEX.preparation_ms] = scratchValue(windowMaxPreparationMs);
      nativeScratch[HISTORY_INDEX.queued_delta] = scratchValue(windowQueuedDelta);
      nativeScratch[HISTORY_INDEX.created_delta] = scratchValue(windowCreatedDelta);
      nativeScratch[HISTORY_INDEX.texture_upload_bytes] = scratchValue(windowTextureUploadBytes);
      nativeScratch[HISTORY_INDEX.source_steps] = scratchValue(windowSourceSteps);
      nativeScratch[HISTORY_INDEX.source_draws] = scratchValue(windowSourceDraws);
      nativeScratch[HISTORY_INDEX.running] = scratchValue(runningValue === null ? null : runningValue ? 1 : 0);
      nativeScratch[HISTORY_INDEX.interval_ms] = scratchValue(windowMaxIntervalMs);
      writeHistoryRow(nativeScratch);
      lastSampleTimestamp = observedAt;
      appendNativePost(observedAt, source === null ? -1 : source, windowMaxTotalMs, windowMaxIntervalMs);
      windowMaxDebtTicks = null;
      windowMaxUpdateMs = null;
      windowMaxDrawMs = null;
      windowMaxTotalMs = null;
      windowMaxPreparationMs = null;
      windowMaxIntervalMs = null;
      windowQueuedDelta = null;
      windowCreatedDelta = null;
      windowTextureUploadBytes = null;
      windowSourceSteps = null;
      windowSourceDraws = null;
    }
    maybeCloseByTime(observedAt);
    return shouldSample;
  }

  function trigger(reason, value, threshold, sourceFrame, scene, clockOwnerCode) {
    const reasonInfo = normalizeReason(reason);
    if (reasonInfo.code === 5 || reasonInfo.code === 6 || reasonInfo.code === 7 || reasonInfo.code === 9) {
      // Pause, resume, render preparation and scheduled pause are expected
      // lifecycle transitions.  They never consume unexpected incident slots.
      const lifecycleKind = reasonInfo.code === 5 ? 'pause' : reasonInfo.code === 6 ? 'resume'
        : reasonInfo.code === 7 ? 'render_preparation' : 'scheduled_pause';
      appendLifecycleEvent(lifecycleKind, {timestamp: finite(now()) ?? 0, sourceFrame, scene});
      return null;
    }
    return snapshotIncident(reason, value, threshold, sourceFrame, scene, clockOwnerCode);
  }

  function eventFields(detail) {
    const source = detail && typeof detail === 'object' ? detail : {};
    const malformedState = {value: false};
    const timestamp = finite(source.timestamp ?? source.startTime ?? source.start_time) ?? finite(now()) ?? 0;
    const sourceFrameValue = integer(source.sourceFrame ?? source.source_frame);
    const sourceFrame = sourceFrameValue === -1 ? null : sourceFrameValue;
    const sceneInfo = normalizeScene(source.scene === undefined || source.scene === null ? currentSceneCode : source.scene);
    const durationMs = copyNumber(source.durationMs ?? source.duration_ms ?? source.duration, malformedState, null);
    const stage = copyNumber(source.stage, malformedState, null);
    const bytes = copyNumber(source.bytes ?? source.byteCount ?? source.byte_count, malformedState, null);
    const files = copyNumber(source.files ?? source.fileCount ?? source.file_count, malformedState, null);
    const suppliedSourceFrame = source.sourceFrame ?? source.source_frame;
    if ((source.timestamp !== undefined && source.timestamp !== null && finite(source.timestamp) === null) ||
        (source.startTime !== undefined && source.startTime !== null && finite(source.startTime) === null) ||
        (source.start_time !== undefined && source.start_time !== null && finite(source.start_time) === null) ||
        (suppliedSourceFrame !== undefined && suppliedSourceFrame !== null && suppliedSourceFrame !== -1 && sourceFrame === null) || sceneInfo.malformed) {
      malformedState.value = true;
    }
    if (malformedState.value) markMalformed();
    return {timestamp, sourceFrame, sceneInfo, durationMs, stage, bytes, files};
  }

  function appendLifecycleEvent(type, detail = {}) {
    if (!active && !paused && !incidents.length) return false;
    const lifecycle = normalizeLifecycle(type);
    if (lifecycle.malformed) lifecycleMalformedCount++;
    lifecycleCount++;
    const fields = eventFields(detail);
    const event = {
      type: 'lifecycle', kind: lifecycle.name, timestamp: fields.timestamp,
      source_frame: fields.sourceFrame === -1 ? null : fields.sourceFrame,
      scene: fields.sceneInfo.name, scene_code: fields.sceneInfo.code,
      duration_ms: fields.durationMs, bytes: fields.bytes, files: fields.files,
    };
    appendRecentEvent(event);
    for (const incident of incidents) {
      if (incident.closed) continue;
      appendPost(incident, event);
      if (lifecycle.name === 'recovered' || lifecycle.name === 'recovery' || lifecycle.name === 'resume') {
        incident.closed = true;
        incident.recovery = lifecycle.name;
        if (incident.recoveryTimer !== null) {
          cancel(incident.recoveryTimer);
          incident.recoveryTimer = null;
        }
      }
    }
    if (fields.sceneInfo.code !== 0 || detail?.scene !== undefined) currentSceneCode = fields.sceneInfo.code;
    return true;
  }

  function lifecycle(type, detail) {
    return appendLifecycleEvent(type, detail);
  }

  function longtask(entry = {}) {
    if (!active && !paused && !incidents.length) return false;
    if (!capabilities.longtask.available) {
      capabilities.longtask.reason = 'unavailable';
    } else {
      capabilities.longtask.observed = true;
      capabilities.longtask.reason = null;
    }
    longtaskCount++;
    const fields = eventFields(entry);
    const event = {
      type: 'longtask', timestamp: fields.timestamp,
      duration_ms: fields.durationMs, source_frame: fields.sourceFrame === -1 ? null : fields.sourceFrame,
    };
    appendRecentEvent(event);
    for (const incident of incidents) if (!incident.closed) appendPost(incident, event);
    return true;
  }

  function audio(
    timestamp, callbackMs, intervalMs, queueDepth, underruns, sourceFrame,
    overflows, clockSeconds, contextState, enabled,
  ) {
    if (timestamp && typeof timestamp === 'object') {
      const observation = timestamp;
      return audio(
        observation.timestamp, observation.callbackMs ?? observation.callback_ms,
        observation.intervalMs ?? observation.interval_ms, observation.queueDepth ?? observation.queue_depth,
        observation.underruns, observation.sourceFrame ?? observation.source_frame,
        observation.overflows, observation.clockSeconds ?? observation.clock_seconds,
        observation.contextState ?? observation.context_state, observation.enabled,
      );
    }
    if (!active && !paused && !incidents.length) return false;
    if (audioConfiguredUnavailable) return false;
    capabilities.audio.available = true;
    capabilities.audio.observed = true;
    capabilities.audio.reason = null;
    const timestampValue = finite(timestamp);
    let malformed = timestampValue === null && timestamp !== undefined && timestamp !== null;
    const at = timestampValue ?? (finite(now()) ?? 0);
    const callbackValue = finite(callbackMs);
    const intervalValue = finite(intervalMs);
    const queueValue = finite(queueDepth);
    const underrunValue = finite(underruns);
    const overflowValue = finite(overflows);
    const clockValue = finite(clockSeconds);
    const normalizedContextState = normalizeAudioState(contextState);
    if ((callbackMs !== undefined && callbackMs !== null && callbackValue === null) ||
        (intervalMs !== undefined && intervalMs !== null && intervalValue === null) ||
        (queueDepth !== undefined && queueDepth !== null && queueValue === null) ||
        (underruns !== undefined && underruns !== null && underrunValue === null) ||
        (overflows !== undefined && overflows !== null && overflowValue === null) ||
        (clockSeconds !== undefined && clockSeconds !== null && clockValue === null) ||
        (contextState !== undefined && contextState !== null && normalizedContextState === 'unknown' && contextState !== 'unknown')) malformed = true;
    const source = integer(sourceFrame);
    if (source === null && sourceFrame !== undefined && sourceFrame !== null) malformed = true;
    if (enabled !== undefined && enabled !== null && enabled !== true && enabled !== false && enabled !== 0 && enabled !== 1) malformed = true;
    if (malformed) { audioMalformedCount++; markMalformed(); }
    audioCount++;
    if (callbackValue !== null) audioAggregate.maxCallbackMs = audioAggregate.maxCallbackMs === null ? callbackValue : Math.max(audioAggregate.maxCallbackMs, callbackValue);
    if (intervalValue !== null) audioAggregate.maxIntervalMs = audioAggregate.maxIntervalMs === null ? intervalValue : Math.max(audioAggregate.maxIntervalMs, intervalValue);
    if (queueValue !== null) audioAggregate.maxQueueDepth = audioAggregate.maxQueueDepth === null ? queueValue : Math.max(audioAggregate.maxQueueDepth, queueValue);
    if (underrunValue !== null) audioAggregate.underruns = audioAggregate.underruns === null ? underrunValue : Math.max(audioAggregate.underruns, underrunValue);
    if (overflowValue !== null) audioAggregate.overflows = audioAggregate.overflows === null ? overflowValue : Math.max(audioAggregate.overflows, overflowValue);
    if (clockValue !== null) audioAggregate.clockSeconds = clockValue;
    audioAggregate.contextState = normalizedContextState;
    audioAggregate.enabled = enabled === undefined || enabled === null ? audioAggregate.enabled : enabled === true;
    audioAggregate.firstTimestamp ??= at;
    audioAggregate.lastTimestamp = at;
    const event = {
      type: 'audio', timestamp: at, callback_ms: callbackValue, interval_ms: intervalValue,
      queue_depth: queueValue, underruns: underrunValue, overflows: overflowValue,
      clock_seconds: clockValue, context_state: normalizedContextState,
      enabled: enabled === undefined || enabled === null ? null : enabled === true,
      source_frame: source === -1 ? null : source,
    };
    appendRecentEvent(event);
    for (const incident of incidents) if (!incident.closed) appendPost(incident, event);
    maybeCloseByTime(at);
    return true;
  }

  function reportFlags() {
    return {
      history_evicted: historyEvicted,
      history_evicted_count: historyEvictedCount,
      incident_evicted: incidentEvicted,
      incident_evicted_count: incidentEvictedCount,
      storage_evicted: storageEvicted,
      storage_evicted_count: storageEvictedCount,
      report_truncated: reportTruncated,
      persistence_failed: persistenceFailed,
      persistence_unavailable: persistenceUnavailable,
      storage_denied: storageDenied,
      quota_exceeded: quotaExceeded,
      malformed_record: malformedRecord,
      malformed_count: malformedCount,
      recent_event_evicted: recentEventEvicted,
      recent_event_evicted_count: recentEventEvictedCount,
      recovery_timed_out: incidents.some(incident => incident.recovery_timed_out),
      clock_regression_count: clockRegressionCount,
      crypto_unavailable: cryptoUnavailable,
    };
  }

  function incidentForReport(incident) {
    return {
      id: incident.id,
      timestamp: incident.timestamp,
      captured_at_ms: incident.captured_at_ms,
      reason: incident.reason,
      reason_code: incident.reason_code,
      value: incident.value,
      threshold: incident.threshold,
      source_frame: incident.source_frame,
      scene: incident.scene,
      scene_code: incident.scene_code,
      clock_owner: incident.clock_owner,
      clock_owner_code: incident.clock_owner_code,
      native: incident.native,
      history: incident.history,
      pre_events: incident.pre_events,
      post_events: incident.post_events,
      post_truncated: incident.post_truncated,
      post_truncated_count: incident.post_truncated_count,
      closed: incident.closed,
      recovery: incident.recovery,
      recovery_timed_out: incident.recovery_timed_out,
    };
  }

  function makeReport() {
    return {
      schema: RUNTIME_DIAGNOSTICS_SCHEMA,
      version: RUNTIME_DIAGNOSTICS_VERSION,
      session_id: sessionId,
      identity,
      environment,
      client,
      active,
      capabilities: clone(capabilities),
      native: nativeSummary(),
      audio: {
        callback_count: audioCount,
        first_timestamp: audioAggregate.firstTimestamp,
        last_timestamp: audioAggregate.lastTimestamp,
        max_callback_ms: audioAggregate.maxCallbackMs,
        max_interval_ms: audioAggregate.maxIntervalMs,
        max_queue_depth: audioAggregate.maxQueueDepth,
        underruns: audioAggregate.underruns,
        overflows: audioAggregate.overflows,
        clock_seconds: audioAggregate.clockSeconds,
        context_state: audioAggregate.contextState,
        enabled: audioAggregate.enabled,
        malformed_count: audioMalformedCount,
      },
      lifecycle: {count: lifecycleCount, malformed_count: lifecycleMalformedCount, longtask_count: longtaskCount},
      limits: {
        history_seconds: historySeconds, history_hz: historyHz, history_capacity: historyCapacity,
        post_event_cap: postEventCap, recent_event_cap: recentEventCap, max_incidents: maxIncidents,
        max_report_bytes: maxReportBytes, max_storage_bytes: maxStorageBytes,
      },
      flags: reportFlags(),
      incidents: incidents.map(incidentForReport),
    };
  }

  function trimReport(report) {
    let encoded = '';
    try { encoded = JSON.stringify(report); } catch { reportTruncated = true; return report; }
    if (encoded.length <= maxReportBytes) return report;
    reportTruncated = true;
    report.flags.report_truncated = true;
    // Truncate oldest incident context first.  Every remaining field is from
    // the strict schema above, so no arbitrary value is retained as a fallback.
    for (const incident of report.incidents) {
      if (encoded.length <= maxReportBytes) break;
      incident.history.rows = incident.history.rows.slice(-Math.max(1, Math.floor(incident.history.rows.length / 2)));
      incident.history.truncated = true;
      try { encoded = JSON.stringify(report); } catch { break; }
    }
    for (const incident of report.incidents) {
      if (encoded.length <= maxReportBytes) break;
      incident.post_events = incident.post_events.slice(-Math.max(1, Math.floor(incident.post_events.length / 2)));
      incident.post_truncated = true;
      try { encoded = JSON.stringify(report); } catch { break; }
    }
    if (encoded.length > maxReportBytes) {
      report.incidents = report.incidents.map(incident => ({
        id: incident.id, timestamp: incident.timestamp, reason: incident.reason,
        captured_at_ms: incident.captured_at_ms,
        reason_code: incident.reason_code, value: incident.value, threshold: incident.threshold,
        source_frame: incident.source_frame, scene: incident.scene, scene_code: incident.scene_code,
        clock_owner: incident.clock_owner, clock_owner_code: incident.clock_owner_code,
        native: incident.native,
        history: {
          seconds: historySeconds, hz: historyHz, sample_interval_ms: sampleIntervalMs,
          columns: [], rows: [], evicted: incident.history.evicted,
          evicted_count: incident.history.evicted_count, truncated: true,
        },
        pre_events: [], post_events: [], post_truncated: true, post_truncated_count: incident.post_truncated_count,
        closed: incident.closed, recovery: incident.recovery, recovery_timed_out: incident.recovery_timed_out,
      }));
    }
    return report;
  }

  function exportReports() {
    return deepFreeze(trimReport(clone(makeReport())));
  }

  function drain(options = {}) {
    const report = exportReports();
    if (options && options.clear === true && !active) {
      incidents = [];
      historyHead = 0;
      historyCount = 0;
      lastSampleTimestamp = null;
    }
    return report;
  }

  function mergeStoredRecords(priorRecords, newRecords) {
    const priorById = new Map();
    const currentById = new Map();
    let malformed = 0;
    for (const item of Array.isArray(priorRecords) ? priorRecords : []) {
      const normalized = normalizeStoredIncident(item);
      if (normalized === null) { malformed++; continue; }
      priorById.set(normalized.id, normalized);
    }
    for (const item of Array.isArray(newRecords) ? newRecords : []) {
      const normalized = normalizeStoredIncident(item);
      if (normalized === null) { malformed++; continue; }
      currentById.set(normalized.id, normalized);
      priorById.delete(normalized.id);
    }
    const current = Array.from(currentById.values()).sort((left, right) =>
      right.incident.captured_at_ms - left.incident.captured_at_ms || left.id.localeCompare(right.id));
    const prior = Array.from(priorById.values()).sort((left, right) =>
      right.incident.captured_at_ms - left.incident.captured_at_ms || left.id.localeCompare(right.id));
    // Preserve the current report's incidents first, even if a wall clock was
    // adjusted backwards.  Older retained records are selected newest-first.
    const candidates = [...current, ...prior];
    const retained = [];
    let bytes = 0;
    let evicted = 0;
    for (const candidate of candidates) {
      if (retained.length >= maxIncidents) { evicted++; continue; }
      const size = utf8Size(candidate);
      if (size === null) { malformed++; continue; }
      if (size > maxStorageBytes || bytes + size > maxStorageBytes) {
        evicted++;
        continue;
      }
      retained.push(candidate);
      bytes += size;
    }
    return {records: retained, bytes, evictedCount: evicted, malformedCount: malformed};
  }

  function storageAdapter() {
    if (storage && typeof storage === 'object') return storage;
    if (!indexedDB || typeof indexedDB.open !== 'function') return null;
    const openDatabase = () => new Promise((resolve, reject) => {
      let request;
      let blocked = false;
      try { request = indexedDB.open(DIAGNOSTICS_DB_NAME, 1); } catch (error) { reject(error); return; }
      request.onupgradeneeded = () => {
        try {
          const db = request.result;
          if (!db.objectStoreNames.contains(DIAGNOSTICS_STORE_NAME)) {
            db.createObjectStore(DIAGNOSTICS_STORE_NAME, {keyPath: 'id'});
          }
        } catch (error) { reject(error); }
      };
      request.onsuccess = () => {
        const db = request.result;
        if (blocked) {
          try { db.close(); } catch { /* optional */ }
          return;
        }
        resolve(db);
      };
      request.onerror = () => reject(request.error || new Error('indexeddb open failed'));
      request.onblocked = () => {
        blocked = true;
        reject(Object.assign(new Error('indexeddb blocked'), {name: 'NotAllowedError'}));
      };
    });
    return {
      async load() {
        const database = await openDatabase();
        try {
          return await new Promise((resolve, reject) => {
            const tx = database.transaction(DIAGNOSTICS_STORE_NAME, 'readonly');
            const store = tx.objectStore(DIAGNOSTICS_STORE_NAME);
            const request = store.openCursor();
            const records = [];
            let overflowed = false;
            request.onsuccess = () => {
              const cursor = request.result;
              if (!cursor) { resolve({records, overflowed}); return; }
              if (records.length < MAX_STORED_CURSOR_RECORDS) records.push(cursor.value);
              else { overflowed = true; resolve({records, overflowed}); return; }
              cursor.continue();
            };
            request.onerror = () => reject(request.error || new Error('indexeddb read failed'));
            tx.onerror = () => reject(tx.error || new Error('indexeddb read failed'));
            tx.onabort = () => reject(tx.error || new Error('indexeddb read aborted'));
          });
        } finally {
          try { database.close(); } catch { /* optional */ }
        }
      },
      async save(records) {
        const database = await openDatabase();
        try {
          await new Promise((resolve, reject) => {
            const tx = database.transaction(DIAGNOSTICS_STORE_NAME, 'readwrite');
            const store = tx.objectStore(DIAGNOSTICS_STORE_NAME);
            store.clear();
            for (const record of records) store.put(record);
            tx.oncomplete = resolve;
            tx.onerror = () => reject(tx.error || new Error('indexeddb write failed'));
            tx.onabort = () => reject(tx.error || new Error('indexeddb write aborted'));
          });
        } finally {
          try { database.close(); } catch { /* optional */ }
        }
      },
      async merge(records) {
        const database = await openDatabase();
        try {
          return await new Promise((resolve, reject) => {
            let tx;
            let failed = false;
            let finishedRead = false;
            let result = null;
            const prior = [];
            let cursorOverflowed = false;
            const fail = (error) => {
              if (failed) return;
              failed = true;
              try { tx?.abort(); } catch { /* transaction may already be closed */ }
              reject(error);
            };
            try {
              tx = database.transaction(DIAGNOSTICS_STORE_NAME, 'readwrite');
              const store = tx.objectStore(DIAGNOSTICS_STORE_NAME);
              const request = store.openCursor();
              const finishRead = () => {
                if (finishedRead) return;
                finishedRead = true;
                result = mergeStoredRecords(prior, records);
                if (cursorOverflowed) result.evictedCount++;
                try {
                  store.clear();
                  for (const record of result.records) store.put(record);
                } catch (error) { fail(error); }
              };
              request.onsuccess = () => {
                const cursor = request.result;
                if (!cursor || prior.length >= MAX_STORED_CURSOR_RECORDS) {
                  if (prior.length >= MAX_STORED_CURSOR_RECORDS && cursor) cursorOverflowed = true;
                  finishRead();
                  return;
                }
                prior.push(cursor.value);
                cursor.continue();
              };
              request.onerror = () => fail(request.error || new Error('indexeddb read failed'));
              tx.oncomplete = () => { if (!failed) resolve(result || {records: [], bytes: 0, evictedCount: 0, malformedCount: 0}); };
              tx.onerror = () => fail(tx.error || new Error('indexeddb write failed'));
              tx.onabort = () => { if (!failed) { failed = true; reject(tx.error || new Error('indexeddb write aborted')); } };
            } catch (error) { fail(error); }
          });
        } finally {
          try { database.close(); } catch { /* optional */ }
        }
      },
    };
  }

  async function saveRecords(adapter, records) {
    if (typeof adapter.save === 'function') return adapter.save(records);
    if (typeof adapter.put === 'function') {
      for (const record of records) await promiseResult(adapter.put(record));
      return;
    }
    if (typeof adapter.add === 'function') {
      for (const record of records) await promiseResult(adapter.add(record));
      return;
    }
    throw Object.assign(new Error('storage unavailable'), {name: 'NotSupportedError'});
  }

  async function loadRecords(adapter) {
    try {
      const loaded = typeof adapter.load === 'function' ? await promiseResult(adapter.load())
        : typeof adapter.getAll === 'function' ? await promiseResult(adapter.getAll()) : [];
      if (loaded && typeof loaded === 'object' && Array.isArray(loaded.records)) {
        if (loaded.overflowed) {
          storageEvicted = true;
          storageEvictedCount++;
        }
        return loaded.records;
      }
      return loaded;
    } catch (error) {
      throw error;
    }
    return [];
  }

  async function persist() {
    if (persistPromise) return persistPromise;
    const adapter = storageAdapter();
    if (!adapter) {
      persistenceUnavailable = true;
      persistPromise = Promise.resolve({persisted: false, reason: 'unavailable', count: 0});
      const result = await persistPromise;
      persistPromise = null;
      return result;
    }
    const report = exportReports();
    if (report.incidents.length === 0) {
      return {persisted: false, reason: 'empty', count: 0, bytes: 0};
    }
    const records = [];
    for (const incident of report.incidents) {
      const record = normalizeStoredIncident({
        id: `${report.session_id}:${incident.id}`,
        schema: RUNTIME_DIAGNOSTICS_SCHEMA,
        version: RUNTIME_DIAGNOSTICS_VERSION,
        session_id: report.session_id,
        identity: report.identity,
        environment: report.environment,
        client: report.client,
        capabilities: report.capabilities,
        audio: report.audio,
        flags: report.flags,
        limits: report.limits,
        incident,
      });
      // normalizeStoredIncident validates the record's shape.  Keep its
      // explicit shape separate from exportReports to avoid storage accepting
      // caller-provided values.
      if (!record) { malformedRecord = true; continue; }
      records.push(record);
    }
    persistPromise = (async () => {
      try {
        const merged = typeof adapter.merge === 'function'
          ? await promiseResult(adapter.merge(records))
          : mergeStoredRecords(await loadRecords(adapter), records);
        const bounded = Array.isArray(merged?.records) ? merged.records : [];
        malformedRecord ||= Number(merged?.malformedCount) > 0;
        if (Number(merged?.evictedCount) > 0) {
          storageEvicted = true;
          storageEvictedCount += merged.evictedCount;
        }
        if (typeof adapter.merge !== 'function') await saveRecords(adapter, bounded);
        persistedCount += bounded.length;
        return {persisted: true, count: bounded.length, bytes: Number(merged?.bytes) || 0};
      } catch (error) {
        persistenceFailed = true;
        const kind = safeErrorKind(error);
        storageDenied ||= kind === 'denied';
        quotaExceeded ||= kind === 'quota';
        return {persisted: false, reason: kind, count: 0};
      } finally {
        persistPromise = null;
      }
    })();
    return persistPromise;
  }

  async function exportRetained() {
    const adapter = storageAdapter();
    if (!adapter) {
      persistenceUnavailable = true;
      return immutable({
        schema: RUNTIME_DIAGNOSTICS_SCHEMA, version: RUNTIME_DIAGNOSTICS_VERSION,
        identity, environment, client, capabilities: clone(capabilities),
        audio: makeReport().audio, flags: reportFlags(), limits: makeReport().limits, records: [],
      });
    }
    try {
      const loaded = await loadRecords(adapter);
      const bounded = mergeStoredRecords(loaded, []);
      malformedRecord ||= bounded.malformedCount > 0;
      if (bounded.evictedCount > 0) {
        storageEvicted = true;
        storageEvictedCount += bounded.evictedCount;
      }
      const records = bounded.records;
      const report = makeReport();
      return immutable({
        schema: RUNTIME_DIAGNOSTICS_SCHEMA, version: RUNTIME_DIAGNOSTICS_VERSION,
        identity, environment, client, capabilities: clone(capabilities),
        audio: report.audio, flags: reportFlags(), limits: report.limits, records,
      });
    } catch (error) {
      persistenceFailed = true;
      const kind = safeErrorKind(error);
      storageDenied ||= kind === 'denied';
      quotaExceeded ||= kind === 'quota';
      return immutable({
        schema: RUNTIME_DIAGNOSTICS_SCHEMA, version: RUNTIME_DIAGNOSTICS_VERSION,
        identity, environment, client, capabilities: clone(capabilities),
        audio: makeReport().audio, flags: reportFlags(), limits: makeReport().limits, records: [],
      });
    }
  }

  const loadRetained = exportRetained;

  function setActive(value) {
    const next = value === true;
    if (next) {
      active = true;
      paused = false;
      inactiveGeneration++;
      pausePersistencePromise = null;
      return Promise.resolve({persisted: false, active: true});
    }
    active = false;
    paused = true;
    const generation = ++inactiveGeneration;
    // Queue the persistence work so a pause callback never serializes a
    // report or opens IndexedDB synchronously.  Awaiting the returned promise
    // still gives callers a deterministic handoff point.
    const queued = new Promise(resolve => {
      schedule(() => {
        if (active || generation !== inactiveGeneration) {
          resolve({persisted: false, reason: 'resumed', count: 0});
          return;
        }
        persist().then(resolve, () => resolve({persisted: false, reason: 'failed', count: 0}));
      }, 0);
    });
    pausePersistencePromise = queued.finally(() => {
      if (inactiveGeneration === generation) pausePersistencePromise = null;
    });
    return pausePersistencePromise;
  }

  return Object.freeze({
    observeNative,
    trigger,
    lifecycle,
    longtask,
    audio,
    setActive,
    drain,
    persist,
    exportRetained,
    loadRetained,
    exportReports,
    constants: Object.freeze({
      schema: RUNTIME_DIAGNOSTICS_SCHEMA,
      version: RUNTIME_DIAGNOSTICS_VERSION,
      history_seconds: historySeconds,
      history_hz: historyHz,
      history_capacity: historyCapacity,
      sample_interval_ms: sampleIntervalMs,
      max_incidents: maxIncidents,
      max_report_bytes: maxReportBytes,
      max_storage_bytes: maxStorageBytes,
    }),
  });
}

export default createRuntimeDiagnostics;
