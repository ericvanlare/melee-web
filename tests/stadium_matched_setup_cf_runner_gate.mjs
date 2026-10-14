const FILE_ID_KEYS = ['path', 'basename', 'bytes', 'sha256'];
const EXPORTS = Object.freeze({
  arm: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_arm',
  queue: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_queue',
  approve: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_approve',
});
export const PREFIX_V2_SCOPE = 'same-run completed Setup, then extracted run2 prefix from action0 through the next draw/tick/draw after dynamic C/F; terminal remains a proper prefix';
export const FULL_V3_SCOPE = 'same-run completed Setup, then exact original7167-action run2 schedule; functional lifetime A and declared C/F only';
export const FULL_COMMON_FIELDS_SEED_V1_SCOPE = 'same-run completed Setup, exact original7167 schedule, then 3501 post-source-tick comparisons of 15 common fields plus one exact RNG seed value from source tick84 through3584 using the separately hash-bound retained idle3500 reference; retain C/F, GO, PAD, counter and ownership checks; observation only';
const PREFIX_V2_EXPORTS = Object.freeze({
  arm: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_v2_arm',
  queue: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_v2_queue',
  approve: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_approve',
});
export const ASYNCIFY_IDLE_PREDICATE = 'Module.stadiumMatchedSetupNativeIdle';
const PREFIX_V2_EXPLICIT_STEP_EXPORTS = Object.freeze({
  arm: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_v2_explicit_arm',
  queue: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_v2_queue',
  step: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_step_once',
  retire_ready: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_retire_ready',
  retire: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_retire',
  retirement_status: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_retirement_status',
  approve: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_approve',
});
const FULL_V3_EXPLICIT_STEP_EXPORTS = Object.freeze({...PREFIX_V2_EXPLICIT_STEP_EXPORTS,
  arm: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_full_explicit_arm'});
function exactKeys(value, keys, label) {
  if (!value || typeof value !== 'object' || Array.isArray(value) ||
      Object.keys(value).sort().join(',') !== [...keys].sort().join(','))
    throw new TypeError(`${label} has an unexpected shape`);
}

function assertIdentity(binding, actual, label) {
  exactKeys(binding, FILE_ID_KEYS, `${label} preflight identity`);
  exactKeys(actual, FILE_ID_KEYS, `${label} actual identity`);
  if (typeof binding.path !== 'string' || typeof binding.basename !== 'string' ||
      !Number.isSafeInteger(binding.bytes) || binding.bytes < 0 ||
      !/^[0-9a-f]{64}$/.test(binding.sha256) ||
      binding.path !== actual.path || binding.basename !== actual.basename ||
      binding.bytes !== actual.bytes || binding.sha256 !== actual.sha256)
    throw new TypeError(`${label} differs from the frozen preflight identity`);
}

export function validateMatchedSetupCfRequest({enabled, explicitStep = false, fullSchedule = false,
  commonFields = false, setup, textTail, packetPath, assemblyPath}) {
  const active = enabled === true;
  if (typeof commonFields !== 'boolean' || (commonFields && (!active || !fullSchedule || explicitStep !== true)))
    throw new TypeError('Per-tick common fields and seed require explicit full-schedule one-action service');
  if (typeof fullSchedule !== 'boolean' || (fullSchedule && (!active || explicitStep !== true)))
    throw new TypeError('Full schedule requires explicit one-action service and continuation opt-in');
  if (explicitStep === true && !active)
    throw new TypeError('Matched Setup C/F explicit stepping requires the continuation opt-in');
  const hasPacket = typeof packetPath === 'string' && packetPath.length > 0;
  const hasAssembly = typeof assemblyPath === 'string' && assemblyPath.length > 0;
  if (active && (!setup || textTail || !hasPacket || !hasAssembly))
    throw new TypeError('Matched Setup C/F requires the exact Setup route, packet, and assembly manifest; the separate SIS probe cannot be combined');
  if (!active && (hasPacket || hasAssembly))
    throw new TypeError('Matched Setup C/F packet paths require the explicit continuation opt-in');
  return active;
}

export function validateMatchedSetupCfPreflight(preflight, {enabled, explicitStep = false, fullSchedule = false,
  commonFields = false, packetPath, assemblyPath, helperPath}) {
  if (typeof commonFields !== 'boolean' || (commonFields &&
      (enabled !== true || fullSchedule !== true || explicitStep !== true)))
    throw new TypeError('Per-tick common-field seed preflight requires explicit full-schedule service opt-in');
  if (typeof fullSchedule !== 'boolean' || (fullSchedule && (enabled !== true || explicitStep !== true)))
    throw new TypeError('Full schedule preflight requires explicit service opt-in');
  if (enabled !== true) return null;
  if (preflight?.route_mode !== 'first-sss-vs-setup')
    throw new TypeError('Matched Setup C/F requires the frozen first-sss-vs-setup preflight route');
  const frozen = preflight.matched_setup_cf;
  exactKeys(frozen, explicitStep === true
    ? ['scope', 'packet', 'assembly_manifest', 'helper', 'diagnostic_exports',
      'asyncify_idle_predicate']
    : ['scope', 'packet', 'assembly_manifest', 'helper', 'diagnostic_exports'],
    'Matched Setup C/F preflight binding');
  if (explicitStep === true && frozen.asyncify_idle_predicate !== ASYNCIFY_IDLE_PREDICATE)
    throw new TypeError('Matched Setup C/F Asyncify idle predicate differs from its private binding');
  if (frozen.scope !== 'same-run completed Setup, then exact original 86-tick/85-draw prefix; compare C before draw84 and F before draw85' &&
      frozen.scope !== PREFIX_V2_SCOPE && frozen.scope !== FULL_V3_SCOPE &&
      frozen.scope !== FULL_COMMON_FIELDS_SEED_V1_SCOPE)
    throw new TypeError('Matched Setup C/F preflight scope differs');
  const expectedPaths = [
    ['packet', packetPath], ['assembly_manifest', assemblyPath], ['helper', helperPath],
  ];
  for (const [name, actualPath] of expectedPaths) {
    const binding = frozen[name];
    if (!binding || typeof binding.path !== 'string' ||
        binding.path !== actualPath || binding.basename !== actualPath.split(/[\\/]/).at(-1))
      throw new TypeError(`Matched Setup C/F ${name} path differs from its frozen preflight`);
    if (!Number.isSafeInteger(binding.bytes) || binding.bytes < 0 ||
        !/^[0-9a-f]{64}$/.test(binding.sha256))
      throw new TypeError(`Matched Setup C/F ${name} identity is malformed`);
  }
  const fullScope = frozen.scope === FULL_V3_SCOPE || frozen.scope === FULL_COMMON_FIELDS_SEED_V1_SCOPE;
  if (fullScope !== fullSchedule)
    throw new TypeError('Full schedule request differs from exact preflight scope');
  if ((frozen.scope === FULL_COMMON_FIELDS_SEED_V1_SCOPE) !== commonFields)
    throw new TypeError('Per-tick common-field seed request differs from exact preflight scope');
  if (explicitStep === true && frozen.scope !== (commonFields ? FULL_COMMON_FIELDS_SEED_V1_SCOPE
      : fullSchedule ? FULL_V3_SCOPE : PREFIX_V2_SCOPE))
    throw new TypeError('Matched Setup C/F explicit stepping differs from its reviewed full/prefix scope');
  exactKeys(frozen.diagnostic_exports, explicitStep === true
    ? ['arm', 'queue', 'step', 'retire_ready', 'retire', 'retirement_status', 'approve']
    : ['arm', 'queue', 'approve'],
    'Matched Setup C/F diagnostic exports');
  const expectedExports = explicitStep === true ? (fullSchedule ? FULL_V3_EXPLICIT_STEP_EXPORTS : PREFIX_V2_EXPLICIT_STEP_EXPORTS)
    : frozen.scope === PREFIX_V2_SCOPE ? PREFIX_V2_EXPORTS : EXPORTS;
  if (JSON.stringify(frozen.diagnostic_exports) !== JSON.stringify(expectedExports))
    throw new TypeError('Matched Setup C/F native exports differ from the reviewed contract');
  return frozen;
}

export function trackMatchedSetupCfExplicitStep(promise, onSettled = () => {}) {
  const tracked = {settled: false, value: undefined, error: null, promise: null,
    requiresAsyncifyIdle: false, asyncifyIdle: false};
  tracked.promise = Promise.resolve(promise).then(value => {
    tracked.settled = true;
    tracked.value = value;
    onSettled(tracked);
    return value;
  }, error => {
    tracked.settled = true;
    tracked.error = error;
    onSettled(tracked);
    throw error;
  });
  return tracked;
}

export function matchedSetupCfNativeAccessAllowed(pendingStep) {
  return !pendingStep || (pendingStep.settled === true &&
    (pendingStep.requiresAsyncifyIdle !== true || pendingStep.asyncifyIdle === true));
}

export function dispatchMatchedSetupCfNativeEvaluation({command, pendingStep, dispatch}) {
  if (command?.requireAsyncifyIdle === true &&
      !matchedSetupCfNativeAccessAllowed(pendingStep))
    throw Error('A prior explicit native step remains unsettled or unsafe');
  if (typeof dispatch !== 'function')
    throw new TypeError('Matched Setup C/F native evaluation requires a dispatch function');
  return dispatch();
}

export async function guardMatchedSetupCfNativeCleanup({explicitStep = false,
  pendingStep = null, forcedUnsafe = false, checkNativeIdle, onNativeAccessAllowed,
  onNativeAccessBlocked}) {
  let idleCheck = 'not-required';
  let error = null;
  if (explicitStep === true) {
    try {
      if (typeof checkNativeIdle !== 'function')
        throw Error('Native idle predicate check is unavailable');
      idleCheck = await checkNativeIdle() === true ? 'idle' : 'not-idle';
    } catch (cause) {
      idleCheck = 'check-failed';
      error = String(cause?.message || cause);
    }
  }
  const pendingSafe = forcedUnsafe !== true &&
    matchedSetupCfNativeAccessAllowed(pendingStep);
  const allowed = explicitStep !== true || (idleCheck === 'idle' && pendingSafe);
  const status = {allowed, idle_check: idleCheck, pending_safe: pendingSafe, error};
  if (allowed) await onNativeAccessAllowed?.(status);
  else await onNativeAccessBlocked?.(status);
  return status;
}

export function validateMatchedSetupCfIdentities(frozen, actual) {
  if (!frozen || !actual || typeof actual !== 'object')
    throw new TypeError('Matched Setup C/F file identities are unavailable');
  for (const name of ['packet', 'assembly_manifest', 'helper'])
    assertIdentity(frozen[name], actual[name], name);
  return true;
}

export function matchedSetupCfRuntimeFailure({native, pageErrors = [],
  consoleErrors = [], consoleWarnings = [], consoleMessages = [],
  consoleMessageCaptureOverflow = false}) {
  if (consoleMessageCaptureOverflow)
    return 'Browser console diagnostic capture overflowed; continuation cannot rule out a missing service or GPU error';
  if (pageErrors.length) return `Browser page error during continuation: ${pageErrors[0]}`;
  if (consoleErrors.length) return `Browser console error during continuation: ${consoleErrors[0]}`;
  if (typeof native?.error === 'string' && native.error.length)
    return `Native runtime error during continuation: ${native.error}`;
  const messages = [native?.message, native?.diagnostics, ...consoleWarnings,
    ...consoleMessages.map(item => item?.text)]
    .filter(value => typeof value === 'string' && value.length);
  if (messages.some(message => message.includes('Ignoring nested GX_CMD_CALL_DL')))
    return 'Missing display-list provenance service: Ignoring nested GX_CMD_CALL_DL';
  if (messages.some(message => /(?:WebGPU|WebGL|GPU).*(?:uncaptured error|validation error|device lost|context lost)|(?:uncaptured error|validation error|device lost|context lost).*(?:WebGPU|WebGL|GPU)/i.test(message)))
    return 'GPU reported an error during the matched continuation';
  return null;
}
