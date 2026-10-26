import {pathToFileURL} from 'node:url';

export const FRESH_SETUP_GO2_ROUTE = 'first-sss-fresh-setup-go2';
export const FRESH132_TYPED22_PAIR_ROUTE = 'first-sss-fresh132-full-pair-typed22';
export const FRESH132_TYPED22_PAIR_SCOPE = 'same retained Fresh132 producer, exact MWC6 full A and freshly recreated full B in one Module; each3501 rows of22 declared address-free fields plus exactseed; no original per-owner lifecycle/RNG or C3 acceptance';
export const FRESH132_FULL_A_ROUTE = 'first-sss-fresh132-full-a';

const FILE_ID_KEYS = ['path', 'basename', 'bytes', 'sha256'];
const EXPORTS = Object.freeze({
  arm: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_arm',
  queue: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_queue',
  approve: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_approve',
});
export const PREFIX_V2_SCOPE = 'same-run completed Setup, then extracted run2 prefix from action0 through the next draw/tick/draw after dynamic C/F; terminal remains a proper prefix';
export const FULL_V3_SCOPE = 'same-run completed Setup, then exact original7167-action run2 schedule; functional lifetime A and declared C/F only';
export const FULL_COMMON_FIELDS_SEED_V1_SCOPE = 'same-run completed Setup, exact original7167 schedule, then 3501 post-source-tick comparisons of 15 common fields plus one exact RNG seed value from source tick84 through3584 using the separately hash-bound retained idle3500 reference; retain C/F, GO, PAD, counter and ownership checks; observation only';
export const FRESH132_FULL_A_SCOPE = 'same retained Fresh132 original Entry2544/Setup2584 producer; exact MWC6 7168-action/3585-tick/3583-draw lifetime A after completed Setup; 3501 comparisons of 15 paired common fields plus exact seed; C/F/observed GO/GO2; no recreation B, full typed parity, per-owner RNG, C3, pixels, PCM or whole-session acceptance';
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

function assertComparatorIdentity(binding, actual, label) {
  const keys = ['path', 'bytes', 'sha256'];
  exactKeys(binding, keys, `${label} preflight identity`);
  exactKeys(actual, keys, `${label} actual identity`);
  if (typeof binding.path !== 'string' || !Number.isSafeInteger(binding.bytes) || binding.bytes < 0 ||
      !/^[0-9a-f]{64}$/.test(binding.sha256) || binding.path !== actual.path ||
      binding.bytes !== actual.bytes || binding.sha256 !== actual.sha256)
    throw new TypeError(`${label} differs from the frozen preflight identity`);
}

const FRESH132_FULL_A_CONTEXT_BINDING_SCHEMA = 'melee-web-stadium-fresh132-full-a-context-comparator-v1';

export function resolveFreshSssReadyStartComparator(preflight, {fresh132FullA = false, fresh132FullPair = false} = {}) {
  if (typeof fresh132FullA !== 'boolean' || typeof fresh132FullPair !== 'boolean' || (fresh132FullPair && !fresh132FullA))
    throw new TypeError('Fresh132 full-A comparator route must be boolean');
  if (!fresh132FullA) {
    if ([FRESH132_FULL_A_ROUTE,FRESH132_TYPED22_PAIR_ROUTE].includes(preflight?.route_mode))
      throw new TypeError('Fresh132 full-A comparator cannot be consumed by the legacy ready-start route');
    return preflight?.first_sss_fresh_ready_start?.comparator;
  }
  if (preflight?.route_mode !== (fresh132FullPair?FRESH132_TYPED22_PAIR_ROUTE:FRESH132_FULL_A_ROUTE))
    throw new TypeError('Fresh132 full-A comparator requires its distinct frozen route');
  const frozen = preflight.first_sss_fresh132_full_a;
  exactKeys(frozen, ['schema', 'context_comparator'], 'Fresh132 full-A context comparator preflight');
  if (frozen.schema !== FRESH132_FULL_A_CONTEXT_BINDING_SCHEMA)
    throw new TypeError('Fresh132 full-A context comparator schema differs');
  exactKeys(frozen.context_comparator, ['path', 'bytes', 'sha256'],
    'Fresh132 full-A context comparator identity');
  return frozen.context_comparator;
}

export function validateFreshSssReadyStartComparator(preflight, {fresh132FullA = false, fresh132FullPair = false, actual} = {}) {
  const binding = resolveFreshSssReadyStartComparator(preflight, {fresh132FullA,fresh132FullPair});
  assertComparatorIdentity(binding, actual, fresh132FullA
    ? 'Fresh132 full-A context comparator' : 'Legacy Fresh132 ready-start comparator');
  return binding;
}

export function validateMatchedSetupCfRequest({enabled, explicitStep = false, fullSchedule = false,
  commonFields = false, fresh132FullA = false, setup, textTail, packetPath, assemblyPath}) {
  const active = enabled === true;
  if (typeof fresh132FullA !== 'boolean' || (fresh132FullA &&
      (!active || explicitStep !== true || fullSchedule || commonFields || !setup || textTail)))
    throw new TypeError('Fresh132 full-A requires its distinct explicit Setup route and MWC6 scope');
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

export function validateMatchedSetupCfPreflight(preflight, {enabled, freshSetupGo2 = false,
  fresh132FullA = false, fresh132FullPair = false, explicitStep = false, fullSchedule = false, commonFields = false,
  packetPath, assemblyPath, helperPath}) {
  if (typeof commonFields !== 'boolean' || (commonFields &&
      (enabled !== true || fullSchedule !== true || explicitStep !== true)))
    throw new TypeError('Per-tick common-field seed preflight requires explicit full-schedule service opt-in');
  if (typeof fullSchedule !== 'boolean' || (fullSchedule && (enabled !== true || explicitStep !== true)))
    throw new TypeError('Full schedule preflight requires explicit service opt-in');
  if (typeof fresh132FullA !== 'boolean' || typeof fresh132FullPair !== 'boolean' || (fresh132FullPair&&!fresh132FullA))
    throw new TypeError('Fresh132 full-A preflight mode must be boolean');
  if (enabled !== true) {
    if (fresh132FullA) throw new TypeError('Fresh132 full-A requires matched Setup continuation opt-in');
    return null;
  }
  if (typeof freshSetupGo2 !== 'boolean' ||
      (freshSetupGo2 && (explicitStep !== true || fullSchedule || commonFields || fresh132FullA)))
    throw new TypeError('Fresh Setup GO+2 preflight requires bounded explicit steps');
  if (fresh132FullA && (explicitStep !== true || freshSetupGo2 || fullSchedule || commonFields))
    throw new TypeError('Fresh132 full-A preflight requires its distinct explicit MWC6 route');
  if (fresh132FullA)
    resolveFreshSssReadyStartComparator(preflight, {fresh132FullA: true,fresh132FullPair});
  const expectedRoute = fresh132FullPair ? FRESH132_TYPED22_PAIR_ROUTE : fresh132FullA ? FRESH132_FULL_A_ROUTE
    : freshSetupGo2 ? FRESH_SETUP_GO2_ROUTE : 'first-sss-vs-setup';
  if (preflight?.route_mode !== expectedRoute)
    throw new TypeError(`Matched Setup C/F requires the frozen ${expectedRoute} preflight route`);
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
      frozen.scope !== FULL_COMMON_FIELDS_SEED_V1_SCOPE && frozen.scope !== FRESH132_FULL_A_SCOPE && frozen.scope !== FRESH132_TYPED22_PAIR_SCOPE)
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
  if (([FRESH132_FULL_A_SCOPE,FRESH132_TYPED22_PAIR_SCOPE].includes(frozen.scope)) !== fresh132FullA)
    throw new TypeError('Fresh132 full-A request differs from exact MWC6 preflight scope');
  if ((frozen.scope === FRESH132_TYPED22_PAIR_SCOPE) !== fresh132FullPair)
    throw new TypeError('Typed22 pair requires its exact preflight scope');
  if (explicitStep === true && frozen.scope !== (fresh132FullPair ? FRESH132_TYPED22_PAIR_SCOPE : fresh132FullA ? FRESH132_FULL_A_SCOPE
      : commonFields ? FULL_COMMON_FIELDS_SEED_V1_SCOPE
      : fullSchedule ? FULL_V3_SCOPE : PREFIX_V2_SCOPE))
    throw new TypeError('Matched Setup C/F explicit stepping differs from its reviewed full/prefix scope');
  exactKeys(frozen.diagnostic_exports, explicitStep === true
    ? ['arm', 'queue', 'step', 'retire_ready', 'retire', 'retirement_status', 'approve']
    : ['arm', 'queue', 'approve'],
    'Matched Setup C/F diagnostic exports');
  const expectedExports = fresh132FullPair ? {...FULL_V3_EXPLICIT_STEP_EXPORTS,
    arm:'_melee_web_native_menu_stadium_fresh132_typed22_explicit_arm'} : explicitStep === true ? (fresh132FullA || fullSchedule
    ? FULL_V3_EXPLICIT_STEP_EXPORTS : PREFIX_V2_EXPLICIT_STEP_EXPORTS)
    : frozen.scope === PREFIX_V2_SCOPE ? PREFIX_V2_EXPORTS : EXPORTS;
  if (Object.keys(expectedExports).some(key => frozen.diagnostic_exports[key] !== expectedExports[key]))
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

export async function validateFreshSetupGo2RunnerSource({helperPath, context, readyExpected}) {
  const helper = await import(pathToFileURL(helperPath).href);
  return helper.validateFreshSetupGo2Expectations(context, readyExpected);
}

export async function validateFresh132FullARunnerSource({helperPath, context, readyExpected}) {
  const [projection, helper] = await Promise.all([
    import(new URL('./fresh132_full_a_projection.mjs', import.meta.url).href),
    import(pathToFileURL(helperPath).href),
  ]);
  if (typeof helper.validateFreshSss132ReadyStartExpectedSource !== 'function' ||
      typeof projection.validateFresh132FullAContext !== 'function')
    throw new TypeError('Fresh132 full-A requires the checked ready-start and source-context validators');
  return projection.validateFresh132FullAContext({entry: context?.entry, setup: context?.setup,
    constructor: context?.constructor_observation}, readyExpected,
  helper.validateFreshSss132ReadyStartExpectedSource);
}

// The actual page evaluator is shared; legacy commands retain their original body.
export function evaluateMatchedSetupCfNativeCommand({kind, scheduleIndex, action, ordinal, bundle, bytes,
    manifestBytes, accepted, requireAsyncifyIdle, gate2Owner}) {
    const module = globalThis.Module;
    const asyncifyIdle = () => typeof module?.stadiumMatchedSetupNativeIdle === 'function' &&
      module.stadiumMatchedSetupNativeIdle() === true;
    if (typeof kind === 'string' && kind.startsWith('gate2-')) {
      if (!gate2Owner || typeof gate2Owner.gate2Pending !== 'function')
        throw Error('Gate2 command lacks its same-page reserved owner');
      const close = kind === 'gate2-retire' || kind === 'gate2-abort';
      gate2Owner.gate2Pending(kind === 'gate2-arm' ? 'arm' :
        kind === 'gate2-retire' ? 'retire' : kind === 'gate2-abort' ? 'abort' : 'action');
      const unsafe = reason => {
        gate2Owner.gate2Uncertain(reason);
        return {result: 0, asyncify_idle: false, message: reason};
      };
      if (!asyncifyIdle()) return unsafe('Gate2 command suppressed: Asyncify is not idle');
      const settled = (result, extra = {}) => {
        // A late native promise must not reopen native access after host/shell uncertainty.
        if (gate2Owner.gate2CanSettle() !== true)
          return {result: 0, asyncify_idle: false, message: 'Gate2 late settlement belongs to an uncertain/stale token'};
        if (!asyncifyIdle()) return unsafe('Gate2 command settled without actual Asyncify idle');
        gate2Owner.gate2Settled();
        const details = typeof extra === 'function' ? extra() : extra;
        return {result, asyncify_idle: true, ...details};
      };
      const readLive = () => {
        const fn = module._melee_web_native_menu_stadium_first_sss_vs_setup_cf_retirement_status;
        if (typeof fn !== 'function') throw Error('Gate2 live receipt export is absent');
        const pointer = fn();
        if (!pointer) throw Error('Gate2 live receipt pointer is absent');
        return JSON.parse(module.UTF8ToString(pointer));
      };
      try {
        if (kind === 'gate2-arm') {
          const fn = module._melee_web_native_menu_stadium_first_sss_vs_setup_cf_full_explicit_arm;
          if (typeof fn !== 'function') throw Error('Gate2 explicit full arm export is absent');
          const approvalName = '__meleeWebStadiumFirstSssVsSetupCfApproved';
          if (Object.hasOwn(globalThis, approvalName)) throw Error('Gate2 Setup approval callback already exists');
          const approve = () => gate2Owner.gate2ApproveSetup();
          let pointer = 0;
          Object.defineProperty(globalThis, approvalName, {value: approve, configurable: true});
          try {
            pointer = module._malloc(manifestBytes.length);
            if (!pointer) throw Error('Gate2 manifest allocation failed');
            module.HEAPU8.set(manifestBytes, pointer);
            const result = fn(pointer, manifestBytes.length);
            if (gate2Owner.gate2CanSettle() !== true)
              return {result: 0, asyncify_idle: false, message: 'Gate2 arm lost its pending token; allocation retained'};
            if (!asyncifyIdle()) return unsafe('Gate2 arm returned with unsettled Asyncify');
            module._free(pointer); pointer = 0;
            return settled(result);
          } finally {
            // On exception/uncertainty the owned allocation dies with the page;
            // no native free is attempted after an uncertain arm outcome.
            if (globalThis[approvalName] === approve) delete globalThis[approvalName];
          }
        }
        if (kind === 'gate2-queue') {
          const fn = module._melee_web_native_menu_stadium_first_sss_vs_setup_cf_v2_queue;
          if (typeof fn !== 'function') throw Error('Gate2 queue export is absent');
          return settled(fn(scheduleIndex, action === 'tick' ? 1 : 2,
            ordinal, action === 'tick' ? bundle : -1));
        }
        if (kind === 'gate2-approve') {
          const fn = module._melee_web_native_menu_stadium_first_sss_vs_setup_cf_approve;
          if (typeof fn !== 'function') throw Error('Gate2 approval export is absent');
          return settled(fn(scheduleIndex, accepted ? 1 : 0));
        }
        if (kind === 'gate2-live') return settled(1, () => ({observation: readLive()}));
        if (kind === 'gate2-retire-ready') {
          const fn = module._melee_web_native_menu_stadium_first_sss_vs_setup_cf_retire_ready;
          if (typeof fn !== 'function') throw Error('Gate2 retirement readiness export is absent');
          return settled(1, {ready: fn()});
        }
        if (kind === 'gate2-step' || close) {
          if (kind === 'gate2-abort') {
            const receipt = readLive();
            if (receipt?.schema !== 'melee-web-stadium-later-b-gate2-native-v1' ||
                receipt.armed !== false || receipt.failed !== true || receipt.complete !== false ||
                receipt.source_ticks !== 0 || receipt.source_draws !== 0 || receipt.cursor !== 0 ||
                receipt.queued !== false || receipt.awaiting_approval !== false || receipt.last_action !== null)
              throw Error('Gate2 abort lacks an actual known-unarmed zero-action receipt');
            // native_menu_unload independently performs the private checked abort
            // readiness/owner/renderer transaction; no generic unload fallback.
          }
          if (typeof module.ccall !== 'function') throw Error('Gate2 command requires generated Asyncify ccall');
          const name = kind === 'gate2-step' ? 'melee_web_native_menu_stadium_first_sss_vs_setup_cf_step_once' :
            kind === 'gate2-retire' ? 'melee_web_native_menu_stadium_first_sss_vs_setup_cf_retire' :
              'melee_web_native_menu_unload';
          const call = module.ccall(name, 'number', [], [], {async: true});
          if (!call || typeof call.then !== 'function') return unsafe('Gate2 ccall did not return its generated promise');
          return Promise.resolve(call).then(result => settled(result,
            kind === 'gate2-step' && result === 0 ? () => {
              const observation = readLive();
              if (observation?.schema !== 'melee-web-stadium-later-b-gate2-native-v1' ||
                  observation.failed !== true || observation.state !== 'failed' ||
                  typeof observation.error !== 'string' || observation.error.length === 0)
                throw Error('Gate2 settled zero step lacks a retained native failed-state message');
              return {failure: {classification: 'settled-native-step-failure',
                message: observation.error, observation}};
            } : kind === 'gate2-retire' && result === 0 ? () => {
              // The ccall promise has resolved, the reserved owner is still
              // correlated, and Asyncify is idle before either retained-state
              // getter runs. A false result may mean a refusal before close or
              // a notification refusal after native close; preserve both facts.
              const nativeStatus = readLive();
              const nativeMessagePointer = module._melee_web_native_menu_message?.();
              const nativeMessage = nativeMessagePointer
                ? module.UTF8ToString(nativeMessagePointer) : null;
              const message = typeof nativeMessage === 'string' && nativeMessage.length
                ? nativeMessage : 'Gate2 native retirement returned zero without a retained message';
              const failure = {classification: 'settled-native-retire-failure', message,
                native_retired: nativeStatus?.native_retired === true, native_status: nativeStatus};
              return {message, failure};
            } : undefined),
            error => unsafe(`Gate2 Asyncify command rejected: ${String(error?.message || error)}`));
        }
        throw Error(`Unknown Gate2 native command ${kind}`);
      } catch (error) {
        // An exception does not establish a zero-action refusal or a safe close.
        return unsafe(`Gate2 command outcome is uncertain: ${String(error?.message || error)}`);
      }
    }
    if (requireAsyncifyIdle && !asyncifyIdle())
      return {result: 0, message: 'Asyncify is not idle; native command was suppressed',
        asyncify_idle: false};
    const readMessage = () => {
      const pointer = module?._melee_web_native_menu_message?.();
      return pointer ? module.UTF8ToString(pointer) : null;
    };
    if (!module) return {result: 0, message: 'Native module is absent'};
    if (kind === 'arm') {
      const fn = module._melee_web_native_menu_stadium_first_sss_vs_setup_cf_arm;
      if (typeof fn !== 'function') return {result: 0, message: 'Matched Setup C/F arm export is absent'};
      const result = fn();
      return {result, message: result === 1 ? null : readMessage()};
    }
    if (kind === 'arm-explicit-v2' || kind === 'arm-explicit-full' || kind === 'arm-explicit-typed22') {
      const fn = kind === 'arm-explicit-typed22'
        ? module._melee_web_native_menu_stadium_fresh132_typed22_explicit_arm : kind === 'arm-explicit-full'
        ? module._melee_web_native_menu_stadium_first_sss_vs_setup_cf_full_explicit_arm
        : module._melee_web_native_menu_stadium_first_sss_vs_setup_cf_v2_explicit_arm;
      if (typeof fn !== 'function') return {result: 0, message: 'Matched Setup C/F explicit arm export is absent'};
      const pointer = module._malloc(manifestBytes.length);
      if (!pointer) return {result: 0, message: 'Matched Setup C/F explicit manifest allocation failed'};
      try {
        module.HEAPU8.set(manifestBytes, pointer);
        const result = fn(pointer, manifestBytes.length);
        return {result, message: result === 1 ? null : readMessage()};
      } finally { module._free(pointer); }
    }
    if (kind === 'arm-v2') {
      const fn = module._melee_web_native_menu_stadium_first_sss_vs_setup_cf_v2_arm;
      if (typeof fn !== 'function') return {result: 0, message: 'Matched Setup C/F v2 arm export is absent'};
      const pointer = module._malloc(manifestBytes.length);
      if (!pointer) return {result: 0, message: 'Matched Setup C/F v2 manifest allocation failed'};
      try {
        module.HEAPU8.set(manifestBytes, pointer);
        const result = fn(pointer, manifestBytes.length);
        return {result, message: result === 1 ? null : readMessage()};
      } finally { module._free(pointer); }
    }
    if (kind === 'queue') {
      const fn = module._melee_web_native_menu_stadium_first_sss_vs_setup_cf_queue;
      if (typeof fn !== 'function') return {result: 0, message: 'Matched Setup C/F queue export is absent'};
      const pointer = bytes.length ? module._malloc(bytes.length) : 0;
      if (bytes.length && !pointer) return {result: 0, message: 'Matched Setup C/F PAD copy allocation failed'};
      try {
        if (bytes.length) module.HEAPU8.set(bytes, pointer);
        const result = fn(scheduleIndex, action === 'tick' ? 1 : 2,
          ordinal, bundle, pointer, bytes.length);
        return {result, message: result === 1 ? null : readMessage()};
      } finally {
        if (pointer) module._free(pointer);
      }
    }
    if (kind === 'step-explicit') {
      if (typeof module.ccall !== 'function')
        throw Error('Matched Setup C/F explicit step requires generated runtime ccall');
      const call = module.ccall(
        'melee_web_native_menu_stadium_first_sss_vs_setup_cf_step_once',
        'number', [], [], {async: true});
      if (!call || typeof call.then !== 'function')
        throw Error('Generated explicit native step did not return its Asyncify promise');
      return Promise.resolve(call).then(result => {
        const idle = asyncifyIdle();
        return {result, message: idle ? result === 1 ? null : readMessage() : null,
          asyncify_idle: idle};
      }, error => {
        const idle = asyncifyIdle();
        const rejection = String(error?.message || error);
        const nativeMessage = idle ? readMessage() : null;
        return {result: 0,
          message: idle ? `${nativeMessage || 'Asyncify native step rejected'}: ${rejection}`
            : `Asyncify became unsafe after native step rejection: ${rejection}`,
          asyncify_idle: idle};
      });
    }
    if (kind === 'retire-ready') {
      if (!asyncifyIdle()) return {ready: -1, asyncify_idle: false};
      const fn = module?._melee_web_native_menu_stadium_first_sss_vs_setup_cf_retire_ready;
      if (typeof fn !== 'function')
        return {ready: -1, asyncify_idle: true, message: 'Matched Setup C/F retire-ready export is absent'};
      return {ready: fn(), asyncify_idle: true};
    }
    if (kind === 'retire-explicit') {
      if (!asyncifyIdle()) return {result: 0, asyncify_idle: false};
      if (typeof module.ccall !== 'function')
        throw Error('Matched Setup C/F retirement requires generated runtime ccall');
      const call = module.ccall(
        'melee_web_native_menu_stadium_first_sss_vs_setup_cf_retire',
        'number', [], [], {async: true});
      if (!call || typeof call.then !== 'function')
        throw Error('Generated native retirement did not return its Asyncify promise');
      const settled = result => {
        const idle = asyncifyIdle();
        if (!idle) return {result, asyncify_idle: false};
        const statusFn = module._melee_web_native_menu_stadium_first_sss_vs_setup_cf_retirement_status;
        let retirement = null;
        if (typeof statusFn === 'function') {
          const pointer = statusFn();
          retirement = pointer ? JSON.parse(module.UTF8ToString(pointer)) : null;
        }
        return {result, asyncify_idle: true, retirement,
          phase: module._melee_web_native_menu_phase(),
          running: module._melee_web_native_menu_running(),
          message: result === 1 ? null : readMessage()};
      };
      return Promise.resolve(call).then(settled, error => {
        const idle = asyncifyIdle();
        if (!idle) return {result: 0, asyncify_idle: false,
          rejection: String(error?.message || error)};
        const response = settled(0);
        response.rejection = String(error?.message || error);
        return response;
      });
    }
    if (kind === 'queue-v2') {
      const fn = module._melee_web_native_menu_stadium_first_sss_vs_setup_cf_v2_queue;
      if (typeof fn !== 'function') return {result: 0, message: 'Matched Setup C/F v2 queue export is absent'};
      const result = fn(scheduleIndex, action === 'tick' ? 1 : 2,
        ordinal, action === 'tick' ? bundle : -1);
      return {result, message: result === 1 ? null : readMessage()};
    }
    if (kind === 'approve') {
      const fn = module._melee_web_native_menu_stadium_first_sss_vs_setup_cf_approve;
      if (typeof fn !== 'function') return {result: 0, message: 'Matched Setup C/F approval export is absent'};
      const result = fn(scheduleIndex, accepted ? 1 : 0);
      return {result, message: result === 1 ? null : readMessage()};
    }
    return {result: 0, message: `Unknown matched Setup C/F native command ${kind}`};
  }

// One adapter for the existing action driver, not a second source-action driver.
// evaluate is the owner's bounded page.evaluate operation; a lost response is sticky.
export function createLaterBGate2NativeAdapter({evaluate, owner, deadline,
  now = Date.now, pause = ms => new Promise(resolve => setTimeout(resolve, ms))}) {
  if (typeof evaluate !== 'function' || !owner || !Number.isFinite(deadline))
    throw Error('Gate2 adapter requires its bounded page owner and deadline');
  let pending = null, unsafe = false, armed = false, armAttempted = false;
  let knownUnarmedRefusal = false, closed = false, actionsStarted = false, unacknowledgedFailure = false;
  let settledFailure = null, settledRetirementFailure = null;
  const execute = async command => {
    if (unsafe || unacknowledgedFailure || settledFailure || settledRetirementFailure || closed || pending || now() >= deadline)
      throw Error('Gate2 native command is closed, pending, failed, uncertain or expired');
    const request = {...command, gate2Owner: owner, requireAsyncifyIdle: true};
    try {
      const promise = dispatchMatchedSetupCfNativeEvaluation({command: request,
        pendingStep: pending, dispatch: () => evaluate(evaluateMatchedSetupCfNativeCommand, request)});
      pending = trackMatchedSetupCfExplicitStep(promise, state => {
        state.asyncifyIdle = state.value?.asyncify_idle === true;
      });
      pending.requiresAsyncifyIdle = true;
      const response = await pending.promise;
      if (response?.asyncify_idle !== true) {
        unsafe = true;
        throw Error(response?.message || 'Gate2 native settlement is uncertain');
      }
      return response;
    } catch (error) {
      unsafe = true; // No follow-up page/native RPC can cancel or classify a lost response.
      throw error;
    } finally { pending = null; }
  };
  const one = async command => {
    const response = await execute(command);
    if (response.result !== 1) {
      if (command.kind === 'gate2-step' &&
          response.failure?.classification === 'settled-native-step-failure') {
        settledFailure = response.failure;
        const error = Error(`Gate2 settled native step refusal: ${response.failure.message}`);
        error.gate2Failure = response.failure;
        throw error;
      }
      if (command.kind === 'gate2-retire' &&
          response.failure?.classification === 'settled-native-retire-failure') {
        settledRetirementFailure = response.failure;
        const error = Error(`Gate2 settled native retirement refusal: ${response.failure.message}`);
        error.gate2Retirement = response.failure;
        throw error;
      }
      throw Error(response.message || `Gate2 ${command.kind} refused`);
    }
    return response;
  };
  const api = {
    async arm(bytes) {
      if (armAttempted) throw Error('Gate2 arm is one-use');
      armAttempted = true;
      const response = await execute({kind: 'gate2-arm', manifestBytes: Array.from(bytes)});
      knownUnarmedRefusal = response.result === 0;
      armed = response.result === 1;
      return response.result;
    },
    async queue(scheduleIndex, action) {
      if (!armed) throw Error('Gate2 queue has no confirmed arm');
      return (await one({kind: 'gate2-queue', scheduleIndex, action: action.action,
        ordinal: action.action === 'tick' ? action.bundle : action.draw,
        bundle: action.action === 'tick' ? action.bundle : null})).result;
    },
    async step() {
      if (!armed) throw Error('Gate2 step has no confirmed arm');
      actionsStarted = true;
      return (await one({kind: 'gate2-step'})).result;
    },
    async observe() {
      if (!armed) throw Error('Gate2 observation has no confirmed arm');
      return (await one({kind: 'gate2-live'})).observation;
    },
    async approve(scheduleIndex, accepted) {
      if (accepted !== true) {
        unacknowledgedFailure = true;
        throw Error('Gate2 mismatch remains unACKed; only owned OS cleanup is permitted');
      }
      return (await one({kind: 'gate2-approve', scheduleIndex, accepted})).result;
    },
    async retire() {
      if (!armed) throw Error('Gate2 retirement requires its confirmed arm');
      while (true) {
        const response = await one({kind: 'gate2-retire-ready'});
        if (response.ready === 1) break;
        if (response.ready !== 0) throw Error('Gate2 native retirement eligibility refused');
        if (now() >= deadline) throw Error('Gate2 retirement exceeded the existing deadline');
        await pause(10);
      }
      const result = await one({kind: 'gate2-retire'});
      closed = true; return result;
    },
    async abortUnarmed() {
      if (!knownUnarmedRefusal || armed || actionsStarted)
        throw Error('Gate2 abort lacks a definite settled unarmed zero-action refusal');
      const result = await one({kind: 'gate2-abort'});
      closed = true; return result;
    },
    state() { return {unsafe, pending: pending !== null, armed, arm_attempted: armAttempted,
      known_unarmed_refusal: knownUnarmedRefusal, actions_started: actionsStarted,
      unacknowledged_failure: unacknowledgedFailure, settled_failure: settledFailure,
      settled_retirement_failure: settledRetirementFailure, closed}; },
  };
  return Object.freeze(api);
}

// Transport orchestration only: all scheduling, PAD, GO and sample acceptance is
// delegated to the existing StadiumSetupCfActionDriver supplied by the caller.
export async function runLaterBGate2ActionContinuation({driver, adapter, actualSetup,
  persist, record, finalize, deadline, now = Date.now}) {
  if (!driver?.laterB || driver.payload?.schedule?.length !== 360 ||
      typeof persist !== 'function' || typeof record !== 'function' || typeof finalize !== 'function')
    throw Error('Gate2 continuation requires the admitted existing driver and evidence sinks');
  const beforeCommand = () => {
    if (now() >= deadline) throw Error('Gate2 continuation exceeded the unchanged replay deadline');
  };
  beforeCommand();
  if (!await driver.arm(true, actualSetup)) {
    await persist({phase: 'arm-refusal', status: driver.status(), transport: adapter.state()});
    if (!adapter.state().known_unarmed_refusal || adapter.state().unsafe)
      throw Error('Gate2 arm failed without a definite settled unarmed outcome; OS cleanup only');
    beforeCommand(); await adapter.abortUnarmed();
    return {outcome: 'known-unarmed-abort', component_pass: false, finalization: await finalize()};
  }
  let accepted = 0;
  for (const action of driver.payload.schedule) {
    beforeCommand();
    if (!await driver.submit(action)) throw Error(driver.status().first_error || 'Gate2 submit refused');
    try {
      await adapter.step();
    } catch (error) {
      const failure = error?.gate2Failure;
      if (failure?.classification === 'settled-native-step-failure')
        await persist({phase: 'settled-native-step-failure', action_index: accepted, failure});
      throw error;
    }
    const observation = await adapter.observe();
    // Preserve the real unmodified receipt before comparison/one-use ACK.
    await persist({phase: 'actual-action-receipt', action_index: accepted, observation});
    if (!await driver.accept(observation))
      throw Error(driver.status().first_error || 'Gate2 comparison refused; pending action is unACKed');
    await record({action_index: accepted, observation, approved: true});
    ++accepted;
  }
  if (accepted !== 360 || driver.status().complete !== true || driver.status().pending)
    throw Error('Gate2 continuation did not approve all actual360 actions');
  beforeCommand(); await adapter.retire();
  const finalization = await finalize(); // Only correlated acknowledged native close; JS-only.
  return {outcome: 'retired', component_pass: true, accepted_actions: accepted,
    driver: driver.status(), finalization};
}
