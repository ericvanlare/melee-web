/** Strict opt-in bounds and lifecycle helpers for a natural-pause diagnosis. */
export const NATURAL_PAUSE_PROTOCOL = Object.freeze({
  mode: 'performance',
  source_cursor_limit: 3000,
  replay_timeout_ms: 60000,
  // Replay-task ceiling, including a 5s margin to reach source-stop cleanup.
  // The strict external owner separately reserves the final 5s of its 95s cap;
  // startup consumes that overall budget and truncation leaves evidence incomplete.
  replay_phase_timeout_ms: 65000,
  overall_timeout_ms: 95000,
  observation_timeout_ms: 5000,
  phase_timeout_ms: 20000,
  replay_poll_interval_ms: 250,
  phase_observation_interval_ms: 1000,
  progress_write_interval_ms: 1000,
  expected_frame_slots: 2,
  trace_buffer_size_kib: 131072,
  trace_stream_cap_bytes: 256 * 1024 * 1024,
  viewport_width: 900,
  viewport_height: 700,
  device_scale_factor: 1,
  auto_resume: false,
  injected_load: false,
  injected_stall: false,
  byte_hash: false,
  cache_control: false,
  staging_slot_override: false,
});

const SHA256 = /^[0-9a-f]{64}$/;
const LOOPBACK = new Set(['localhost', '127.0.0.1', '::1', '[::1]']);

export function resolveCaptureMode(mode = 'state', diagnosticManifest = null) {
  if (!['state', 'performance'].includes(mode)) throw Error('Capture mode must be state or performance');
  if (mode === 'performance' && !diagnosticManifest)
    throw Error('Performance capture requires --diagnostic-manifest');
  if (mode === 'state' && diagnosticManifest)
    throw Error('--diagnostic-manifest is restricted to performance capture');
  return {mode, diagnostic: mode === 'performance'};
}

function validInputIdentity(value, label) {
  if (!value || typeof value.path !== 'string' || !pathIsAbsolute(value.path) ||
      !Number.isSafeInteger(value.bytes) || value.bytes <= 0 || !SHA256.test(value.sha256 || ''))
    throw Error(`Invalid frozen ${label} identity`);
}

// Kept tiny here so the manifest validator has no filesystem dependency.
function pathIsAbsolute(value) {
  return typeof value === 'string' && (value.startsWith('/') || /^[A-Za-z]:[\\/]/.test(value));
}

export function validateNaturalPauseManifest(manifest, artifactNames = []) {
  if (manifest?.schema !== 'melee-web-natural-pause-diagnostic-manifest-v1')
    throw Error('Unsupported natural-pause diagnostic manifest schema');
  const source = manifest.source;
  if (!source || !/^[0-9a-f]{40}$/.test(source.commit || '') ||
      !/^[0-9a-f]{40}$/.test(source.tree || ''))
    throw Error('Natural-pause manifest needs exact source commit and tree identities');
  const build = manifest.build;
  if (!build || build.configuration !== 'Release' || build.target !== 'runtime' ||
      typeof build.directory !== 'string' || !pathIsAbsolute(build.directory))
    throw Error('Natural-pause manifest needs an absolute Release runtime build identity');
  const artifacts = build.artifacts;
  if (!artifacts || typeof artifacts !== 'object' || Array.isArray(artifacts))
    throw Error('Natural-pause manifest has no build artifact identity map');
  const names = artifactNames.length ? artifactNames : Object.keys(artifacts);
  if (!names.length || new Set(names).size !== names.length || Object.keys(artifacts).length !== names.length ||
      names.some(name => !Object.hasOwn(artifacts, name)))
    throw Error('Natural-pause build artifact inventory is incomplete or unexpected');
  for (const name of names) {
    const identity = artifacts[name];
    if (!identity || !Number.isSafeInteger(identity.bytes) || identity.bytes <= 0 ||
        !SHA256.test(identity.sha256 || ''))
      throw Error(`Invalid Release artifact identity: ${name}`);
  }
  const browser = manifest.browser;
  if (!browser || !pathIsAbsolute(browser.executable_path) ||
      typeof browser.version !== 'string' || !/^\d+(?:\.\d+){2,3}$/.test(browser.version) ||
      !pathIsAbsolute(browser.profile_path))
    throw Error('Natural-pause manifest needs an exact installed Chrome and absent profile identity');
  validInputIdentity(manifest.inputs?.disc, 'disc');
  validInputIdentity(manifest.inputs?.recipe, 'recipe');
  const header = manifest.inputs.recipe.header;
  if (!header || ![8, 9].includes(header.version) ||
      !Number.isSafeInteger(header.seed) || header.seed < 0 || header.seed > 0xffffffff ||
      !Number.isSafeInteger(header.frames) || header.frames < 1 || header.frames > 108000)
    throw Error('Natural-pause recipe header is not a supported MWRC v8/v9 identity');
  const runtimeUrl = new URL(manifest.runtime_url);
  if (!['http:', 'https:'].includes(runtimeUrl.protocol) || !LOOPBACK.has(runtimeUrl.hostname) ||
      !runtimeUrl.pathname.endsWith('/runtime.html'))
    throw Error('Natural-pause manifest must bind a loopback runtime.html URL');
  const protocol = manifest.protocol;
  if (!protocol || typeof protocol !== 'object' || Array.isArray(protocol) ||
      Object.keys(protocol).length !== Object.keys(NATURAL_PAUSE_PROTOCOL).length ||
      Object.entries(NATURAL_PAUSE_PROTOCOL).some(([name, value]) => protocol[name] !== value))
    throw Error('Natural-pause protocol differs from the reviewed fixed bounds');
  return manifest;
}

export function validateNaturalPauseBrowserIdentity(expected, actual) {
  const matches = !!expected && !!actual &&
    expected.executable_path === actual.executable_path &&
    expected.version === actual.version &&
    expected.profile_path === actual.profile_path &&
    actual.profile_existed_before_launch === false;
  return {valid: matches, expected: expected ?? null, observed: actual ?? null};
}

export function naturalPauseRuntimeUrl(value) {
  const url = new URL(value);
  if (!['http:', 'https:'].includes(url.protocol) || !LOOPBACK.has(url.hostname) ||
      !url.pathname.endsWith('/runtime.html'))
    throw Error('A loopback HTTP runtime.html URL is required for performance capture');
  for (const name of ['melee-web-staging-slots', 'melee-web-staging-byte-hash',
    'render-cache', 'hitch-causal', 'hitch-synchronous-cache', 'hitch-load',
    'hitch-stall', 'hitch-ui-paint', 'controlled-contention']) {
    if (url.searchParams.has(name)) throw Error(`Performance capture forbids ${name}`);
  }
  for (const [name, value] of [['hitch-capture', '1'], ['hitch-marks', '1'],
    ['melee-web-staging-diagnostics', '1']]) {
    if (url.searchParams.has(name) && url.searchParams.get(name) !== value)
      throw Error(`Performance capture requires ${name}=${value}`);
  }
  url.searchParams.set('hitch-capture', '1');
  url.searchParams.set('hitch-marks', '1');
  url.searchParams.set('melee-web-staging-diagnostics', '1');
  return url;
}

export function validateDefaultTwoRingStatus(status) {
  const selection = status?.selection;
  const problems = [];
  if (status?.frame_slots !== 2 || status?.staging_buffers !== 2) problems.push('slot_count_not_two');
  if (selection?.requested_frame_slots !== null || selection?.selected_frame_slots !== 2 ||
      selection?.reason !== 'default_two') problems.push('default_two_selection_not_observed');
  if (selection?.byte_hash_requested !== false || selection?.byte_hash_enabled !== false)
    problems.push('byte_hash_not_disabled');
  if (selection?.staging_diagnostics_requested !== true ||
      selection?.staging_diagnostics_enabled !== true ||
      selection?.staging_diagnostics_reason !== 'explicit_loopback_opt_in')
    problems.push('staging_diagnostics_not_enabled');
  return {valid: problems.length === 0, problems, observed: status ?? null,
    requested_slots: null, selected_slots: 2, selection: 'default_two'};
}

export function firstNaturalPauseStop(snapshot, elapsedMs) {
  // Hook rows retain emission order. Classify the first terminal incident,
  // rather than letting a later error replace an already observed pause.
  const terminalIncident = snapshot?.incidents?.find(event => event.reason === 4 ||
    (snapshot?.source_running === 0 && [1, 2, 3, 8].includes(event.reason)));
  if (terminalIncident)
    return {outcome: terminalIncident.reason === 4 ? 'runtime_error' : 'timing_pause',
      incident: terminalIncident};
  if (snapshot?.runtime_error || snapshot?.dialog_error)
    return {outcome: 'runtime_error', error: snapshot.runtime_error || snapshot.dialog_error};
  if ((snapshot?.native_message?.startsWith('Paused after a timing disruption') ||
      snapshot?.status_text?.startsWith('Paused after a timing disruption')) &&
      snapshot?.source_running === 0)
    return {outcome: 'timing_pause', incident: null};
  if (snapshot?.source_cursor >= NATURAL_PAUSE_PROTOCOL.source_cursor_limit)
    return {outcome: 'cursor_limit', cursor: snapshot.source_cursor};
  if (elapsedMs >= NATURAL_PAUSE_PROTOCOL.replay_timeout_ms)
    return {outcome: 'replay_timeout', elapsed_ms: elapsedMs};
  return null;
}

/** Stop source playback before finalizing/streaming or reading large evidence. */
export async function stopSourceBeforeNaturalPauseExport({readStatus, stopPlayback,
  finalizeTrace, readEvidence, cleanupAfterEvidence}) {
  const before = await readStatus();
  if (before?.source_running !== 0) await stopPlayback();
  const stopped = await readStatus();
  if (stopped?.source_running !== 0)
    throw Error('Refusing large diagnostic exports while native source playback is active');
  const trace = await finalizeTrace();
  const evidence = await readEvidence();
  const cleanup = cleanupAfterEvidence ? await cleanupAfterEvidence() : null;
  return {before, stopped, trace, evidence, cleanup};
}
