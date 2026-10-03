/*
 * Strict, privacy-bounded diagnostics wire schema v1.
 *
 * The browser recorder may retain a richer local aggregate. The adapter maps
 * that aggregate into this fixed shape before submitting it. Keeping the
 * allowlist here gives the browser adapter and Pages Function one owner for
 * validation and canonicalization.
 */

export const DIAGNOSTICS_SCHEMA = 'melee-web-diagnostics';
export const DIAGNOSTICS_VERSION = 1;
export const MAX_REPORT_BYTES = 64 * 1024;
export const MAX_HISTORY_ROWS = 100;
export const MAX_HISTORY_COLUMNS = 20;
export const MAX_PRE_EVENTS = 32;
export const MAX_POST_EVENTS = 24;

const SOURCE_COMMIT_RE = /^[a-f0-9]{40}$/;
const RUNTIME_HASH_RE = /^[a-f0-9]{16}$/;
const SESSION_ID_RE = /^session-[a-z0-9]{1,64}$/;
const INCIDENT_ID_RE = /^(?:incident-[0-9]+|session-[a-z0-9]{1,64}:incident-[0-9]+)$/;
const NUMBER_LIMIT = 1e12;
const FRAME_LIMIT = 0x7fffffff;

export const ENVIRONMENTS = Object.freeze(['staging', 'production']);
export const BROWSER_FAMILIES = Object.freeze(['chrome', 'edge', 'firefox', 'opera', 'safari', 'unknown']);
export const PLATFORMS = Object.freeze(['macos', 'windows', 'linux', 'ios', 'android', 'unknown']);
export const SCENES = Object.freeze(['css', 'preparing', 'sss', 'unloaded', 'match', 'results', 'prize', 'title', 'main', 'opening', 'opening_vs', 'unknown']);
export const CLOCK_OWNERS = Object.freeze(['other', 'simulation', 'audio']);
export const REASONS = Object.freeze([
  'simulation_debt', 'audio_debt', 'nonfinite_clock', 'runtime_failure',
  'manual_pause', 'manual_resume', 'render_preparation', 'clock_regression',
  'scheduled_pause',
]);
export const LIFECYCLE_EVENT_NAMES = Object.freeze([
  'active', 'inactive', 'pause', 'resume', 'visibility_hidden', 'visibility_visible',
  'pagehide', 'pageshow', 'scene_enter', 'scene_exit', 'recovered', 'recovery',
  'timeout', 'unknown', 'scheduled_pause', 'render_preparation', 'freeze',
  'preparation', 'preparation_done', 'asset_preparation',
]);
export const AUDIO_CONTEXT_STATES = Object.freeze(['running', 'suspended', 'closed', 'interrupted', 'unknown']);
export const HISTORY_COLUMNS = Object.freeze([
  'timestamp', 'source_frame', 'scene', 'stage', 'fighter0', 'fighter1',
  'fighter2', 'fighter3', 'debt_ticks', 'update_ms', 'draw_ms', 'total_ms',
  'preparation_ms', 'queued_delta', 'created_delta', 'texture_upload_bytes',
  'source_steps', 'source_draws', 'running', 'interval_ms',
]);

const REASON_CODES = Object.freeze({
  simulation_debt: 1, audio_debt: 2, nonfinite_clock: 3, runtime_failure: 4,
  manual_pause: 5, manual_resume: 6, render_preparation: 7,
  clock_regression: 8, scheduled_pause: 9,
});
const SCENE_CODE_NAMES = Object.freeze({
  0: 'unknown', 1: 'css', 2: 'preparing', 3: 'sss', 4: 'preparing',
  5: 'preparing', 6: 'unloaded', 7: 'match', 8: 'results', 9: 'prize',
  10: 'title', 11: 'main', 12: 'opening', 13: 'opening_vs',
});
const SCENE_NAME_CODES = Object.freeze({
  unknown: 0, css: 1, preparing: 2, sss: 3, unloaded: 6, match: 7,
  results: 8, prize: 9, title: 10, main: 11, opening: 12, opening_vs: 13,
});
const CAPABILITY_NAMES = Object.freeze(['native', 'audio', 'longtask']);
const CAPABILITY_REASONS = Object.freeze(['not_observed', 'unavailable', 'supported_no_events']);

export class DiagnosticSchemaError extends Error {
  constructor(code, path, message = code) {
    super(message);
    this.name = 'DiagnosticSchemaError';
    this.code = code;
    this.path = path;
  }
}

function fail(code, path, message = code) {
  throw new DiagnosticSchemaError(code, path, message);
}

function isRecord(value) {
  return value !== null && typeof value === 'object' && !Array.isArray(value);
}

function exactKeys(value, expected, path) {
  if (!isRecord(value)) fail('type', path, 'expected object');
  const actual = Object.keys(value);
  const allowed = new Set(expected);
  if (actual.length !== expected.length || actual.some(key => !allowed.has(key))) fail('unknown_key', path);
}

function enumValue(value, allowed, path) {
  if (typeof value !== 'string' || !allowed.includes(value)) fail('enum', path);
  return value;
}

function boundedNumber(value, path, { integer = false, min = -NUMBER_LIMIT, max = NUMBER_LIMIT } = {}) {
  if (typeof value !== 'number' || !Number.isFinite(value) || value < min || value > max) fail('number', path);
  if (integer && !Number.isInteger(value)) fail('integer', path);
  return value;
}

function nullableNumber(value, path, options) {
  return value === null ? null : boundedNumber(value, path, options);
}

function boundedId(value, pattern, path) {
  if (typeof value !== 'string' || !pattern.test(value)) fail('id', path);
  return value;
}

function normalizeIdentity(value, path) {
  exactKeys(value, ['source_commit', 'runtime_hash', 'build_profile'], path);
  boundedId(value.source_commit, SOURCE_COMMIT_RE, `${path}.source_commit`);
  boundedId(value.runtime_hash, RUNTIME_HASH_RE, `${path}.runtime_hash`);
  if (typeof value.build_profile !== 'string' || !['player', 'audio-preview', 'audio-player'].includes(value.build_profile)) fail('identity', `${path}.build_profile`);
  return { source_commit: value.source_commit, runtime_hash: value.runtime_hash, build_profile: value.build_profile };
}

function normalizeOrigin(value, path) {
  if (typeof value !== 'string' || value.length > 160) fail('origin', path);
  let parsed;
  try { parsed = new URL(value); } catch (_) { fail('origin', path); }
  if (parsed.protocol !== 'https:' || parsed.origin !== value || parsed.pathname !== '/' || parsed.search || parsed.hash || parsed.username || parsed.password) fail('origin', path);
  return value;
}

function normalizeEnvironment(value, path) {
  exactKeys(value, ['env', 'origin'], path);
  return { env: enumValue(value.env, ENVIRONMENTS, `${path}.env`), origin: normalizeOrigin(value.origin, `${path}.origin`) };
}

function normalizeClient(value, path) {
  exactKeys(value, ['browser_family', 'browser_major', 'platform'], path);
  enumValue(value.browser_family, BROWSER_FAMILIES, `${path}.browser_family`);
  if (value.browser_major !== null) boundedNumber(value.browser_major, `${path}.browser_major`, { integer: true, min: 0, max: 999 });
  enumValue(value.platform, PLATFORMS, `${path}.platform`);
  return { browser_family: value.browser_family, browser_major: value.browser_major, platform: value.platform };
}

function normalizeCapabilities(value, path) {
  exactKeys(value, CAPABILITY_NAMES, path);
  const result = {};
  for (const name of CAPABILITY_NAMES) {
    const capability = value[name];
    exactKeys(capability, ['available', 'observed', 'reason'], `${path}.${name}`);
    if (typeof capability.available !== 'boolean' || typeof capability.observed !== 'boolean') fail('capability', `${path}.${name}`);
    if (capability.reason !== null && !CAPABILITY_REASONS.includes(capability.reason)) fail('capability', `${path}.${name}.reason`);
    if (capability.observed && capability.reason !== null) fail('capability', `${path}.${name}.reason`);
    if (!capability.observed && capability.reason === null) fail('capability', `${path}.${name}.reason`);
    result[name] = { available: capability.available, observed: capability.observed, reason: capability.reason };
  }
  return result;
}

function normalizeScene(value, path) {
  exactKeys(value, ['scene', 'scene_code'], path);
  enumValue(value.scene, SCENES, `${path}.scene`);
  boundedNumber(value.scene_code, `${path}.scene_code`, { integer: true, min: 0, max: 255 });
  if (SCENE_CODE_NAMES[value.scene_code] !== value.scene) fail('scene', path);
  return { scene: value.scene, scene_code: value.scene_code };
}

function normalizeIncident(value, path) {
  exactKeys(value, ['reason', 'reason_code', 'value', 'threshold', 'source_frame', 'scene', 'scene_code', 'clock_owner', 'clock_owner_code'], path);
  enumValue(value.reason, REASONS, `${path}.reason`);
  boundedNumber(value.reason_code, `${path}.reason_code`, { integer: true, min: 1, max: 9 });
  if (REASON_CODES[value.reason] !== value.reason_code) fail('reason', path);
  const scene = normalizeScene({ scene: value.scene, scene_code: value.scene_code }, path);
  enumValue(value.clock_owner, CLOCK_OWNERS, `${path}.clock_owner`);
  boundedNumber(value.clock_owner_code, `${path}.clock_owner_code`, { integer: true, min: 0, max: 2 });
  if (CLOCK_OWNERS[value.clock_owner_code] !== value.clock_owner) fail('clock_owner', path);
  return {
    reason: value.reason,
    reason_code: value.reason_code,
    value: nullableNumber(value.value, `${path}.value`),
    threshold: nullableNumber(value.threshold, `${path}.threshold`),
    source_frame: value.source_frame === null ? null : boundedNumber(value.source_frame, `${path}.source_frame`, { integer: true, min: 0, max: FRAME_LIMIT }),
    scene: scene.scene,
    scene_code: scene.scene_code,
    clock_owner: value.clock_owner,
    clock_owner_code: value.clock_owner_code,
  };
}

function normalizeHistory(value, path) {
  exactKeys(value, ['columns', 'rows', 'evicted', 'evicted_count', 'truncated'], path);
  if (!Array.isArray(value.columns) || value.columns.length !== MAX_HISTORY_COLUMNS || value.columns.some((column, index) => column !== HISTORY_COLUMNS[index])) fail('history_columns', `${path}.columns`);
  if (!Array.isArray(value.rows) || value.rows.length > MAX_HISTORY_ROWS) fail('history_rows', `${path}.rows`);
  const rows = value.rows.map((row, rowIndex) => {
    if (!Array.isArray(row) || row.length !== MAX_HISTORY_COLUMNS) fail('history_width', `${path}.rows[${rowIndex}]`);
    return row.map((cell, columnIndex) => cell === null ? null : boundedNumber(cell, `${path}.rows[${rowIndex}][${columnIndex}]`));
  });
  if (typeof value.evicted !== 'boolean' || typeof value.truncated !== 'boolean') fail('history_flags', path);
  boundedNumber(value.evicted_count, `${path}.evicted_count`, { integer: true, min: 0, max: FRAME_LIMIT });
  return { columns: [...HISTORY_COLUMNS], rows, evicted: value.evicted, evicted_count: value.evicted_count, truncated: value.truncated };
}

function normalizeEvent(value, path) {
  if (!isRecord(value) || typeof value.type !== 'string') fail('event', path);
  if (value.type === 'native') {
    exactKeys(value, ['type', 'timestamp', 'source_frame', 'total_ms', 'interval_ms'], path);
    return { type: 'native', timestamp: nullableNumber(value.timestamp, `${path}.timestamp`, { min: 0 }), source_frame: value.source_frame === null ? null : boundedNumber(value.source_frame, `${path}.source_frame`, { integer: true, min: 0, max: FRAME_LIMIT }), total_ms: nullableNumber(value.total_ms, `${path}.total_ms`), interval_ms: nullableNumber(value.interval_ms, `${path}.interval_ms`) };
  }
  if (value.type === 'lifecycle') {
    exactKeys(value, ['type', 'kind', 'timestamp', 'source_frame', 'scene', 'scene_code', 'duration_ms', 'bytes', 'files'], path);
    enumValue(value.kind, LIFECYCLE_EVENT_NAMES, `${path}.kind`);
    const scene = normalizeScene({ scene: value.scene, scene_code: value.scene_code }, `${path}.scene`);
    return { type: 'lifecycle', kind: value.kind, timestamp: nullableNumber(value.timestamp, `${path}.timestamp`, { min: 0 }), source_frame: value.source_frame === null ? null : boundedNumber(value.source_frame, `${path}.source_frame`, { integer: true, min: 0, max: FRAME_LIMIT }), scene: scene.scene, scene_code: scene.scene_code, duration_ms: nullableNumber(value.duration_ms, `${path}.duration_ms`), bytes: nullableNumber(value.bytes, `${path}.bytes`), files: nullableNumber(value.files, `${path}.files`) };
  }
  if (value.type === 'longtask') {
    exactKeys(value, ['type', 'timestamp', 'duration_ms', 'source_frame'], path);
    return { type: 'longtask', timestamp: nullableNumber(value.timestamp, `${path}.timestamp`, { min: 0 }), duration_ms: nullableNumber(value.duration_ms, `${path}.duration_ms`), source_frame: value.source_frame === null ? null : boundedNumber(value.source_frame, `${path}.source_frame`, { integer: true, min: 0, max: FRAME_LIMIT }) };
  }
  if (value.type === 'audio') {
    exactKeys(value, ['type', 'timestamp', 'callback_ms', 'interval_ms', 'queue_depth', 'underruns', 'overflows', 'clock_seconds', 'context_state', 'enabled', 'source_frame'], path);
    enumValue(value.context_state, AUDIO_CONTEXT_STATES, `${path}.context_state`);
    if (value.enabled !== null && typeof value.enabled !== 'boolean') fail('event', `${path}.enabled`);
    return { type: 'audio', timestamp: nullableNumber(value.timestamp, `${path}.timestamp`, { min: 0 }), callback_ms: nullableNumber(value.callback_ms, `${path}.callback_ms`), interval_ms: nullableNumber(value.interval_ms, `${path}.interval_ms`), queue_depth: nullableNumber(value.queue_depth, `${path}.queue_depth`), underruns: nullableNumber(value.underruns, `${path}.underruns`), overflows: nullableNumber(value.overflows, `${path}.overflows`), clock_seconds: nullableNumber(value.clock_seconds, `${path}.clock_seconds`), context_state: value.context_state, enabled: value.enabled, source_frame: value.source_frame === null ? null : boundedNumber(value.source_frame, `${path}.source_frame`, { integer: true, min: 0, max: FRAME_LIMIT }) };
  }
  fail('event_type', path);
}

function normalizeEvents(value, path) {
  exactKeys(value, ['pre', 'post'], path);
  if (!Array.isArray(value.pre) || value.pre.length > MAX_PRE_EVENTS) fail('pre_events', `${path}.pre`);
  if (!Array.isArray(value.post) || value.post.length > MAX_POST_EVENTS) fail('post_events', `${path}.post`);
  return { pre: value.pre.map((event, index) => normalizeEvent(event, `${path}.pre[${index}]`)), post: value.post.map((event, index) => normalizeEvent(event, `${path}.post[${index}]`)) };
}

function normalizeFlags(value, path) {
  exactKeys(value, ['incomplete', 'persistence_failure'], path);
  if (typeof value.incomplete !== 'boolean' || typeof value.persistence_failure !== 'boolean') fail('flags', path);
  return { incomplete: value.incomplete, persistence_failure: value.persistence_failure };
}

export function normalizeDiagnosticReport(value) {
  exactKeys(value, ['schema', 'version', 'session_id', 'incident_id', 'identity', 'environment', 'client', 'capabilities', 'incident', 'history', 'events', 'flags'], 'report');
  if (value.schema !== DIAGNOSTICS_SCHEMA) fail('schema', 'report.schema');
  if (value.version !== DIAGNOSTICS_VERSION) fail('version', 'report.version');
  return {
    schema: DIAGNOSTICS_SCHEMA,
    version: DIAGNOSTICS_VERSION,
    session_id: boundedId(value.session_id, SESSION_ID_RE, 'report.session_id'),
    incident_id: boundedId(value.incident_id, INCIDENT_ID_RE, 'report.incident_id'),
    identity: normalizeIdentity(value.identity, 'report.identity'),
    environment: normalizeEnvironment(value.environment, 'report.environment'),
    client: normalizeClient(value.client, 'report.client'),
    capabilities: normalizeCapabilities(value.capabilities, 'report.capabilities'),
    incident: normalizeIncident(value.incident, 'report.incident'),
    history: normalizeHistory(value.history, 'report.history'),
    events: normalizeEvents(value.events, 'report.events'),
    flags: normalizeFlags(value.flags, 'report.flags'),
  };
}

function skipWhitespace(text, state) {
  while (state.index < text.length && /\s/.test(text[state.index])) state.index += 1;
}

function parseJsonString(text, state) {
  const start = state.index;
  if (text[state.index] !== '"') throw new SyntaxError('expected string');
  state.index += 1;
  let escaped = false;
  while (state.index < text.length) {
    const character = text[state.index];
    if (escaped) { escaped = false; state.index += 1; continue; }
    if (character === '\\') { escaped = true; state.index += 1; continue; }
    if (character === '"') { state.index += 1; return JSON.parse(text.slice(start, state.index)); }
    if (character === '\n' || character === '\r' || character.charCodeAt(0) < 0x20) throw new SyntaxError('control character in string');
    state.index += 1;
  }
  throw new SyntaxError('unterminated string');
}

function parseJsonValue(text, state, depth) {
  if (depth > 16) throw new SyntaxError('JSON nesting is too deep');
  skipWhitespace(text, state);
  const character = text[state.index];
  if (character === '"') return parseJsonString(text, state);
  if (character === '{') {
    state.index += 1;
    const object = Object.create(null);
    const keys = new Set();
    skipWhitespace(text, state);
    if (text[state.index] === '}') { state.index += 1; return object; }
    while (state.index < text.length) {
      skipWhitespace(text, state);
      const key = parseJsonString(text, state);
      if (keys.has(key)) throw new SyntaxError('duplicate object key');
      keys.add(key);
      if (keys.size > 64) throw new SyntaxError('too many object keys');
      skipWhitespace(text, state);
      if (text[state.index] !== ':') throw new SyntaxError('expected colon');
      state.index += 1;
      object[key] = parseJsonValue(text, state, depth + 1);
      skipWhitespace(text, state);
      if (text[state.index] === '}') { state.index += 1; return object; }
      if (text[state.index] !== ',') throw new SyntaxError('expected comma');
      state.index += 1;
    }
    throw new SyntaxError('unterminated object');
  }
  if (character === '[') {
    state.index += 1;
    const array = [];
    skipWhitespace(text, state);
    if (text[state.index] === ']') { state.index += 1; return array; }
    while (state.index < text.length) {
      if (array.length >= 128) throw new SyntaxError('too many array items');
      array.push(parseJsonValue(text, state, depth + 1));
      skipWhitespace(text, state);
      if (text[state.index] === ']') { state.index += 1; return array; }
      if (text[state.index] !== ',') throw new SyntaxError('expected comma');
      state.index += 1;
    }
    throw new SyntaxError('unterminated array');
  }
  for (const [literal, value] of [['true', true], ['false', false], ['null', null]]) {
    if (text.startsWith(literal, state.index)) { state.index += literal.length; return value; }
  }
  const number = text.slice(state.index).match(/^-?(?:0|[1-9]\d*)(?:\.\d+)?(?:[eE][+-]?\d+)?/);
  if (number) {
    state.index += number[0].length;
    const value = Number(number[0]);
    if (!Number.isFinite(value)) throw new SyntaxError('non-finite number');
    return value;
  }
  throw new SyntaxError('invalid JSON value');
}

export function parseStrictJson(text) {
  if (typeof text !== 'string') throw new SyntaxError('expected text');
  const state = { index: 0 };
  const value = parseJsonValue(text, state, 0);
  skipWhitespace(text, state);
  if (state.index !== text.length) throw new SyntaxError('trailing JSON data');
  return value;
}

export function canonicalizeReport(report) {
  return JSON.stringify(normalizeDiagnosticReport(report));
}
