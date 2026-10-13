const FILE_ID_KEYS = ['path', 'basename', 'bytes', 'sha256'];
const EXPORTS = Object.freeze({
  arm: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_arm',
  queue: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_queue',
  approve: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_approve',
});
export const PREFIX_V2_SCOPE = 'same-run completed Setup, then extracted run2 prefix from action0 through the next draw/tick/draw after dynamic C/F; terminal remains a proper prefix';
const PREFIX_V2_EXPORTS = Object.freeze({
  arm: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_v2_arm',
  queue: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_v2_queue',
  approve: '_melee_web_native_menu_stadium_first_sss_vs_setup_cf_approve',
});
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

export function validateMatchedSetupCfRequest({enabled, setup, textTail,
  packetPath, assemblyPath}) {
  const active = enabled === true;
  const hasPacket = typeof packetPath === 'string' && packetPath.length > 0;
  const hasAssembly = typeof assemblyPath === 'string' && assemblyPath.length > 0;
  if (active && (!setup || textTail || !hasPacket || !hasAssembly))
    throw new TypeError('Matched Setup C/F requires the exact Setup route, packet, and assembly manifest; the separate SIS probe cannot be combined');
  if (!active && (hasPacket || hasAssembly))
    throw new TypeError('Matched Setup C/F packet paths require the explicit continuation opt-in');
  return active;
}

export function validateMatchedSetupCfPreflight(preflight, {enabled, packetPath,
  assemblyPath, helperPath}) {
  if (enabled !== true) return null;
  if (preflight?.route_mode !== 'first-sss-vs-setup')
    throw new TypeError('Matched Setup C/F requires the frozen first-sss-vs-setup preflight route');
  const frozen = preflight.matched_setup_cf;
  exactKeys(frozen, ['scope', 'packet', 'assembly_manifest', 'helper', 'diagnostic_exports'],
    'Matched Setup C/F preflight binding');
  if (frozen.scope !== 'same-run completed Setup, then exact original 86-tick/85-draw prefix; compare C before draw84 and F before draw85' &&
      frozen.scope !== PREFIX_V2_SCOPE)
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
  exactKeys(frozen.diagnostic_exports, ['arm', 'queue', 'approve'],
    'Matched Setup C/F diagnostic exports');
  const expectedExports = frozen.scope === PREFIX_V2_SCOPE ? PREFIX_V2_EXPORTS : EXPORTS;
  if (JSON.stringify(frozen.diagnostic_exports) !== JSON.stringify(expectedExports))
    throw new TypeError('Matched Setup C/F native exports differ from the reviewed contract');
  return frozen;
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
