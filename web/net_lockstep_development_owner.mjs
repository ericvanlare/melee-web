const ROOM_ID_PATTERN = /^[A-Za-z0-9_-]{22,64}$/;
const WASM_SHA256_PATTERN = /^[a-f0-9]{64}$/;

function validateConfig(config) {
  if (!config || typeof config !== 'object' || Array.isArray(config) ||
      Object.keys(config).sort().join(',') !== 'role,roomId,timeoutMs,url' ||
      !['alpha', 'beta'].includes(config.role) || typeof config.url !== 'string' ||
      !/^wss?:\/\//.test(config.url) || typeof config.roomId !== 'string' ||
      !ROOM_ID_PATTERN.test(config.roomId) || !Number.isSafeInteger(config.timeoutMs) ||
      config.timeoutMs <= 0)
    throw Error('Runtime lockstep configuration is invalid');
  return Object.freeze({...config});
}

function assertAvailable(context) {
  if (!context?.ready || context.fatal || !context.bundle || context.replayActive ||
      context.ownerState !== 'prepared')
    throw Error('Runtime lockstep session is unavailable or does not own a fresh prepared disc');
  // Native begin_lockstep requires the CREATED host before launch enters CSS.
  if (context.phase !== 0)
    throw Error('Runtime lockstep requires the fresh prepared native context');
}

/** Owns the development-only start boundary for one runtime lockstep session. */
export function createDevelopmentLockstepOwner({Module, owner, inputDelay, sourceTickLimit, getContext, getIdentity,
  createSession, runNativeStart} = {}) {
  if (!Module || typeof Module !== 'object' || !owner || typeof owner.attachNetworkSession !== 'function' ||
      !Number.isSafeInteger(inputDelay) || inputDelay < 0 ||
      !Number.isSafeInteger(sourceTickLimit) || sourceTickLimit <= inputDelay ||
      typeof owner.stop !== 'function' || typeof getContext !== 'function' ||
      typeof getIdentity !== 'function' || typeof createSession !== 'function' ||
      typeof runNativeStart !== 'function')
    throw Error('Development lockstep owner requires the shared runtime and start operations');

  let config = null, session = null, starting = false, used = false;

  function configure(value) {
    assertAvailable(getContext());
    if (config || used || starting || session)
      throw Error('Runtime lockstep configuration is unavailable or already owned');
    config = validateConfig(value);
    return true;
  }

  async function begin(seed, sourceTicks) {
    if (!config) throw Error('Runtime lockstep configuration is missing');
    if (used || starting || session) throw Error('This player already owns or used a lockstep session');
    assertAvailable(getContext());
    if (!Number.isSafeInteger(seed) || seed < 0 || seed > 0xffffffff ||
        !Number.isSafeInteger(sourceTicks) || sourceTicks <= inputDelay || sourceTicks > sourceTickLimit)
      throw Error('Runtime lockstep seed or source tick bound is invalid');

    // Reserve synchronously so a second caller cannot pass while Wasm identity
    // is being fetched. This read precedes adapter allocation and native entry.
    starting = true;
    let attached = false;
    try {
      const identity = await getIdentity();
      assertAvailable(getContext());
      if (!identity || !WASM_SHA256_PATTERN.test(identity.wasm) || identity.algorithm !== 'sha256')
        throw Error('Runtime lockstep Wasm identity is invalid');

      const ownedSession = createSession({Module, role: config.role, sourceTicks,
        inputTicks: sourceTicks - inputDelay, checksumEvidenceRecords: sourceTicks, url: config.url, roomId: config.roomId,
        timeoutMs: config.timeoutMs});
      session = ownedSession;
      owner.attachNetworkSession(options => ownedSession.close(options));
      attached = true;
      used = true;
      const ready = ownedSession.start(nativeStart => ({
        protocol: 'melee-web-local-lockstep-a2-v1', seed, source_ticks: sourceTicks,
        input_ticks: sourceTicks - inputDelay, input_delay: inputDelay,
        neutral_prefix: {ticks: inputDelay, ports_0_1: 'zero-PADStatus', ports_2_3: 'PAD_ERR_NO_CONTROLLER'},
        pad_encoding: 'MWNI-v1-port-records-11-byte',
        port_ownership: {0: 'alpha', 1: 'beta', 2: 'no-controller', 3: 'no-controller'},
        input_source: 'browser-local-native-PADStatus', runtime_wasm_sha256: identity.wasm,
        disc: identity.disc, native_start: nativeStart,
      }));
      ready.catch(() => {});
      await runNativeStart({seed, sourceTicks});
      return await ready;
    } catch (error) {
      if (session) {
        if (attached) owner.stop(error);
        try { await session.close({mode: 'fatal'}); } catch {}
      }
      throw error;
    } finally {
      starting = false;
    }
  }

  return Object.freeze({configure, begin, onFrame: () => session?.onFrame(),
    close: options => session?.close(options),
    health: options => session?.health(options) ?? null,
    snapshot: () => session?.snapshot() ?? null,
    state: () => Object.freeze({configured: Boolean(config), starting, used, attached: Boolean(session)})});
}
