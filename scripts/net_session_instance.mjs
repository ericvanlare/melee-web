/**
 * One headless Chrome process that runs a development networked-session
 * instance (Track A1). Transport-free: the caller pushes agreed PAD frames and
 * drains per-tick checksum records through the _melee_web_net_* exports. It
 * reuses the shared browser tools and driver, adds no source input except the
 * agreed frames, and never resumes anything except an instrumented timing pause
 * (recorded in `timingResumes`).
 */
import fs from 'node:fs/promises';
import {createHash} from 'node:crypto';
import path from 'node:path';
import {browserLaunchOptions} from './browser_tools.mjs';
import {createBrowserDriver} from './browser_driver.mjs';
import {attachWasmResponseIdentityObserver} from './net_lockstep_observers.mjs';
import {installNetSourceAccounting, readNetSourceAccounting} from './net_source_accounting.mjs';
import {NET_FRAME_BYTES, NET_RECORD_BYTES} from './net_lockstep_core.mjs';

export {NET_FRAME_BYTES, NET_RECORD_BYTES};

export function firstFatalBrowserError(errors) {
  return errors.find(error => error.kind !== 'requestfailed') ?? null;
}

// Installed once per page. The binary adapter owns all network ABI scratch
// memory; Base64 remains only at the Node/Playwright boundary used by A1.
export const PAGE_HELPERS = async (loadNativeAdapter = async () =>
  (await import('./net_lockstep_native_adapter.mjs')).createNetLockstepNativeAdapter) => {
  const createNativeAdapter = await loadNativeAdapter();
  const nativeAdapter = createNativeAdapter(Module, {subscribeProgress: callback => {
    if (!window.__netSourceAccounting) throw Error('Diagnostic native progress requires source accounting');
    return window.__netSourceAccounting.subscribeProgress(callback);
  }});
  window.__meleeWebNetNativeAdapter = nativeAdapter;
  const toBase64 = bytes => {
    let text = '';
    for (let i = 0; i < bytes.length; i += 0x8000)
      text += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
    return btoa(text);
  };
  const fromBase64 = base64 => {
    const text = atob(base64), bytes = new Uint8Array(text.length);
    for (let i = 0; i < text.length; ++i) bytes[i] = text.charCodeAt(i);
    return bytes;
  };
  window.__net = {
    push(base64) { return nativeAdapter.push(fromBase64(base64)); },
    pushIndexed(firstTick, base64) { return nativeAdapter.pushIndexed(firstTick, fromBase64(base64)); },
    configureLocalInputCapture(port, ticks) { return nativeAdapter.configureLocalInputCapture(port, ticks); },
    confirmStart() { return nativeAdapter.confirmStart(); },
    terminate(kind, tick, channel) { return nativeAdapter.terminate(kind, tick, channel); },
    drain(max) {
      const result = nativeAdapter.drain(max);
      return {count: result.count, data: result.count ? toBase64(result.bytes) : ''};
    },
    status() { return nativeAdapter.status(); },
    renderSource() { return Module.UTF8ToString(Module._melee_web_native_menu_diagnostics()); },
    native() {
      return {
        phase: Module._melee_web_native_menu_phase(),
        running: Module._melee_web_native_menu_running(),
        message: Module.UTF8ToString(Module._melee_web_native_menu_message()),
        error: document.querySelector('#status')?.dataset.runtimeError || null,
        status: document.querySelector('#status')?.textContent?.slice(-300) || null,
      };
    },
    observe() {
      const out = {css: null, sss: null};
      if (Module._melee_web_native_menu_phase() === 1) {
        const ids = Module._malloc(56), geometry = Module._malloc(32);
        try {
          out.css = [];
          for (let port = 0; port < 4; ++port) {
            if (!Module._melee_web_css_observe_port(port, 8, ids, geometry)) { out.css.push(null); continue; }
            out.css.push({ids: Array.from(Module.HEAP32.subarray(ids >> 2, (ids >> 2) + 14)),
              geometry: Array.from(Module.HEAPF32.subarray(geometry >> 2, (geometry >> 2) + 8))});
          }
        } finally { Module._free(ids); Module._free(geometry); }
        out.setup = window.menuObserveCssSetup?.() ?? null;
      }
      if (Module._melee_web_native_menu_match_observe) {
        const match = Module.UTF8ToString(Module._melee_web_native_menu_match_observe());
        out.match = JSON.parse(match);
      }
      return out;
    },
    resumeTimingPause() {
      if (typeof window.meleeNetCanResumeTimingPause !== 'function' ||
          !window.meleeNetCanResumeTimingPause()) return false;
      return window.meleeNetResumeTimingPause();
    },
  };
  window.__meleeWebNetNativePeerApi = () => {
    return {
      pushIndexed: (tick, bytes) => nativeAdapter.pushIndexed(tick, bytes),
      configureLocalInputCapture: (port, ticks) => window.__net.configureLocalInputCapture(port, ticks),
      confirmStart: () => window.__net.confirmStart(),
      terminate: (...args) => window.__net.terminate(...args),
      status: () => window.__net.status(),
      subscribeProgress: callback => nativeAdapter.subscribeProgress(callback),
      drain: max => {
        const result = nativeAdapter.drain(max), records = [];
        if (result.bytes.length !== result.count * nativeAdapter.recordBytes)
          throw Error('Native checksum byte count differs');
        for (let offset = 0; offset < result.bytes.length; offset += nativeAdapter.recordBytes)
          records.push(Array.from(result.bytes.subarray(offset, offset + nativeAdapter.recordBytes)));
        return records;
      },
      // This peer borrows the instance-owned adapter. openNetInstance.close()
      // disposes it only after peer and accounting work have joined.
    };
  };
};

export async function closePageNativeNetworkOwnership() {
  const failures = [], cleanup = {runtime_lockstep: null, legacy_peer_closed: false,
    native_adapter_disposed: false};
  const runtimeBefore = window.meleeNetRuntimeLockstepSnapshot?.() ?? null;
  if (runtimeBefore) {
    const mode = runtimeBefore.closing && runtimeBefore.close_mode === 'normal' ? 'normal' : 'fatal';
    try {
      cleanup.runtime_lockstep = {mode, before: runtimeBefore,
        result: await window.meleeNetCloseRuntimeLockstep(mode),
        after: window.meleeNetRuntimeLockstepSnapshot?.() ?? null};
    } catch (error) { failures.push(error); cleanup.runtime_lockstep = {mode, before: runtimeBefore,
      after: window.meleeNetRuntimeLockstepSnapshot?.() ?? null,
      error: String(error?.stack || error?.message || error)}; }
  } else {
    try {
      if (window.__netPeer) { await window.__netPeer.close({intentional: true}); cleanup.legacy_peer_closed = true; }
    } catch (error) { failures.push(error); }
    try {
      window.__meleeWebNetNativeAdapter?.dispose();
      cleanup.native_adapter_disposed = Boolean(window.__meleeWebNetNativeAdapter);
    } catch (error) { failures.push(error); }
  }
  cleanup.errors = failures.map(error => String(error?.stack || error?.message || error));
  return cleanup;
}

export async function openNetInstance({chromium, launchOptions, url, disc, userDataDir, label,
  throttle = 1, arenaFill = -1, timeoutMs = 120000, deadline = Infinity, peerModuleHashes = null,
  syntheticGamepad = null, runtimeOwned = false}) {
  await fs.mkdir(path.resolve(userDataDir), {recursive: true});
  const context = await chromium.launchPersistentContext(path.resolve(userDataDir), {
    ...browserLaunchOptions(launchOptions, {timeout: timeoutMs}),
    viewport: {width: 900, height: 700}, deviceScaleFactor: 1,
  });
  let page, driver, instance, wasmResponses, nativeNetworkCleanup = null,
    closed = false, closeComplete = false, closeOperation = null;
  const close = () => {
    if (closeOperation) return closeOperation;
    closed = true;
    closeOperation = (async () => {
      const failures = [];
      const addFailure = error => error instanceof AggregateError
        ? error.errors.forEach(addFailure) : failures.push(error);
      if (page && typeof page.evaluate === 'function') {
        try {
          nativeNetworkCleanup = await bounded(() => page.evaluate(closePageNativeNetworkOwnership));
          if (instance) instance.nativeNetworkCleanup = nativeNetworkCleanup;
          for (const message of nativeNetworkCleanup?.errors ?? []) failures.push(Error(message));
        } catch (error) { addFailure(error); }
      }
      try { driver?.dispose(); } catch {}
      try { await wasmResponses?.detach(); } catch (error) { addFailure(error); }
      let browser;
      try { browser = context.browser(); } catch {}
      try { await context.close(); closeComplete = true; }
      catch (error) {
        if (browser) {
          try { await browser.close(); closeComplete = true; }
          catch (browserError) { addFailure(error); addFailure(browserError); }
        } else addFailure(error);
      }
      if (instance) instance.closed = closeComplete;
      if (failures.length) throw new AggregateError(failures, 'Network browser instance cleanup failed');
      return closeComplete;
    })();
    return closeOperation;
  };
  const remaining = () => {
    const ms = Math.min(timeoutMs, deadline - Date.now());
    if (ms <= 0) throw Error('Network determinism wall-time bound exhausted');
    return Math.max(1, Math.floor(ms));
  };
  const bounded = async operation => {
    const ms = remaining();
    if (page) {
      page.setDefaultTimeout(ms);
      page.setDefaultNavigationTimeout(ms);
    }
    let timer;
    try {
      return await Promise.race([
        Promise.resolve().then(operation),
        new Promise((_, reject) => { timer = setTimeout(() => reject(Error('Network determinism wall-time bound exhausted')), ms); }),
      ]);
    } finally { clearTimeout(timer); }
  };

  try {
    page = context.pages()[0] || await bounded(() => context.newPage());
    const errors = [];
    const peerModules = peerModuleHashes ? createPeerModuleResponseObserver({url, peerModuleHashes,
      runtimeArtifactNames: JSON.parse(await fs.readFile(new URL('../tools/browser_build_artifacts.json', import.meta.url), 'utf8')),
      onFailure: error => noteError({kind: 'peer-module-identity', message: String(error?.stack || error)}),
    }) : null;
    if (peerModules) page.on('response', response => peerModules.observe(response));
    let browserPeerAllocated = runtimeOwned;
    const noteError = error => { if (errors.length < 32) errors.push(error); };
    const wasmCdp = await bounded(() => context.newCDPSession(page));
    wasmResponses = await bounded(() => attachWasmResponseIdentityObserver(wasmCdp, {
      expectedUrl: new URL('gameplay_menu_browser.wasm', url).href,
    }));
    page.on('pageerror', error => noteError({kind: 'pageerror', message: String(error.stack || error.message)}));
    page.on('console', message => { if (message.type() === 'error') noteError({kind: 'console', message: message.text()}); });
    page.on('response', response => {
      if (response.status() >= 400) noteError({kind: 'http', status: response.status(), url: response.url()});
    });
    page.on('requestfailed', request => noteError({kind: 'requestfailed', method: request.method(),
      url: request.url(), failure: request.failure()?.errorText || null}));
    page.on('request', request => { if (request.method() !== 'GET') noteError({kind: 'unexpected-request', method: request.method(), url: request.url()}); });
    await bounded(() => page.addInitScript(() => {
      window.__meleeNativeRuntimeReady = false;
      const module = globalThis.Module || {};
      module.onRuntimeInitialized = () => { window.__meleeNativeRuntimeReady = true; };
      globalThis.Module = module;
    }));
    if (syntheticGamepad) await bounded(() => page.addInitScript(pad => {
      window.testPad = pad;
      window.testPads = Array.from({length: pad.index + 1}, () => null);
      window.testPads[pad.index] = window.testPad;
      window.__meleeSyntheticPadState = 'neutral';
      window.__meleeSyntheticPadTransition = state => {
        if (!['neutral', 'A', 'release'].includes(state)) throw Error('Unknown synthetic PAD sample state');
        window.__meleeSyntheticPadState = state;
        window.testPad.buttons[0] = state === 'A' ? {pressed: true, value: 1} : {pressed: false, value: 0};
      };
      Object.defineProperty(navigator, 'getGamepads', {configurable: true,
        value: () => window.testPads});
    }, syntheticGamepad));
    instance = {label, page, context, errors, timingResumes: [], throttle, arenaFill, closed: false, close};
    driver = createBrowserDriver(page, {surface: 'development', timeoutMs, deadline});
    instance.driver = driver;
    instance.freezePeerModuleIdentity = () => bounded(() => peerModules.freeze());
    instance.finishPeerModuleIdentity = () => bounded(() => peerModules.freeze({closed: true}));
    instance.createBrowserPeer = options => {
      if (!peerModules) throw Error('Browser peer requires expected module hashes');
      browserPeerAllocated = true;
      return bounded(() => page.evaluate(async options => {
        if (window.__netPeer) throw Error('Browser native peer already allocated');
        const {createBrowserNativePeer} = await import('./net_lockstep_browser_peer.mjs');
        window.__netPeer = createBrowserNativePeer({...options, native: window.__meleeWebNetNativePeerApi()});
        return window.__netPeer.snapshot();
      }, options));
    };
    instance.installLocalWebRtcPeerFactory = options => {
      if (!peerModules) throw Error('Browser peer requires expected module hashes');
      if (browserPeerAllocated) throw Error('Browser native peer factory was already installed');
      browserPeerAllocated = true;
      return bounded(() => page.evaluate(async options => {
        if (window.__meleeCreateBrowserNativePeerForChannel)
          throw Error('Local WebRTC native peer factory was already installed');
        const [{createBrowserNativePeer}, {createDataChannelEndpoint}] = await Promise.all([
          import('./net_lockstep_browser_peer.mjs'), import('./net_lockstep_webrtc.mjs'),
        ]);
        const native = window.__meleeWebNetNativePeerApi();
        if (!native || typeof native.pushIndexed !== 'function' ||
            typeof native.configureLocalInputCapture !== 'function' ||
            typeof native.confirmStart !== 'function' || typeof native.terminate !== 'function' ||
            typeof native.status !== 'function' || typeof native.drain !== 'function')
          throw Error('Local WebRTC browser native adapter is incomplete');
        window.__meleeCreateBrowserNativePeerForChannel = channel => {
          if (window.__netPeer) throw Error('Browser native peer already allocated');
          if (!channel || channel.readyState === 'closed')
            throw Error('Local WebRTC data channel is unavailable for native peer attachment');
          window.__netPeer = createBrowserNativePeer({...options,
            native}, {createEndpoint: endpointOptions =>
            createDataChannelEndpoint({...endpointOptions, channel})});
          window.__meleeLocalWebRtcAttach = {ready_state: channel.readyState,
            ordered: channel.ordered, max_retransmits: channel.maxRetransmits,
            max_packet_lifetime: channel.maxPacketLifeTime};
          return window.__netPeer.snapshot();
        };
        return true;
      }, options));
    };
    instance.prepareLocalWebRtcReceiver = () => bounded(() => page.evaluate(() => {
      if (window.__meleeLocalWebRtc) throw Error('Local WebRTC peer connection was already created');
      const pc = new RTCPeerConnection({iceServers: []});
      let resolvePeer, rejectPeer;
      const peerCreated = new Promise((resolve, reject) => { resolvePeer = resolve; rejectPeer = reject; });
      peerCreated.catch(() => {});
      const state = window.__meleeLocalWebRtc = {role: 'beta', pc, channel: null,
        attach_source: null, ready_state_at_attach: null, attach_error: null, peerCreated};
      pc.addEventListener('datachannel', event => {
        try {
          if (state.channel) throw Error('Remote WebRTC data channel was announced more than once');
          state.channel = event.channel;
          state.attach_source = 'datachannel';
          state.ready_state_at_attach = event.channel.readyState;
          const snapshot = window.__meleeCreateBrowserNativePeerForChannel(event.channel);
          resolvePeer(snapshot);
        } catch (error) {
          state.attach_error = String(error?.stack || error?.message || error);
          rejectPeer(error);
        }
      }, {once: true});
      return {role: state.role, receiver_registered: true};
    }));
    instance.startRoomSignaledLocalWebRtc = options => {
      const pageOptions = {...options,
        timeoutMs: Math.min(options.timeoutMs, Math.max(1, Math.floor(remaining())))};
      return bounded(() => page.evaluate(async options => {
      if (window.__meleeLocalWebRtc) throw Error('Local WebRTC peer connection was already created');
      if (typeof window.__meleeCreateBrowserNativePeerForChannel !== 'function')
        throw Error('Local WebRTC native peer factory was not installed');
      const {createRoomWebRtcSignaler} = await import('./net_lockstep_webrtc_signaling.mjs');
      const pc = new RTCPeerConnection({iceServers: []});
      let resolvePeer, rejectPeer;
      const peerCreated = new Promise((resolve, reject) => { resolvePeer = resolve; rejectPeer = reject; });
      peerCreated.catch(() => {});
      const state = window.__meleeLocalWebRtc = {role: options.role, pc, channel: null,
        attach_source: null, ready_state_at_attach: null, attach_error: null, peerCreated,
        signaler: null, local_candidate_types: [], remote_candidate_types: []};
      const attach = (channel, source) => {
        try {
          if (state.channel) throw Error('duplicate-channel');
          state.channel = channel;
          state.attach_source = source;
          state.ready_state_at_attach = channel.readyState;
          resolvePeer(window.__meleeCreateBrowserNativePeerForChannel(channel));
        } catch {
          state.attach_error = 'datachannel-attachment-failed';
          rejectPeer(Error('datachannel-attachment-failed'));
        }
      };
      if (options.role === 'alpha') {
        const channel = pc.createDataChannel('a3-native-input', {ordered: true});
        attach(channel, 'createDataChannel');
      } else {
        // Register before constructing the signaling endpoint or awaiting its
        // READY barrier. The channel can be announced as open in this event.
        pc.addEventListener('datachannel', event => attach(event.channel, 'datachannel'), {once: true});
      }

      const waitForIce = async () => {
        if (pc.iceGatheringState === 'complete') return;
        await new Promise((resolve, reject) => {
          const finish = error => {
            clearTimeout(timer);
            pc.removeEventListener('icegatheringstatechange', changed);
            if (error) reject(error); else resolve();
          };
          const changed = () => { if (pc.iceGatheringState === 'complete') finish(); };
          const timer = setTimeout(() => finish(Error('ice-gathering-timeout')), options.timeoutMs);
          pc.addEventListener('icegatheringstatechange', changed);
          changed();
        });
      };
      const candidateTypes = description => description?.sdp?.split(/\r?\n/)
        .filter(line => line.startsWith('a=candidate:')).map(line => line.trim().split(/\s+/)[7]) ?? [];
      const checkHostCandidates = (description, destination) => {
        const types = candidateTypes(description);
        if (!types.length || types.some(type => type !== 'host')) throw Error('host-candidate-contract-failed');
        state[destination] = types;
      };
      const waitForConnected = async () => {
        let peerTimer;
        try {
          await Promise.race([peerCreated, new Promise((_, reject) =>
            { peerTimer = setTimeout(() => reject(Error('datachannel-attachment-timeout')), options.timeoutMs); })]);
        } finally { clearTimeout(peerTimer); }
        const channel = state.channel;
        if (!channel) throw Error('datachannel-attachment-failed');
        await new Promise((resolve, reject) => {
          let timer;
          const cleanup = () => {
            clearTimeout(timer);
            pc.removeEventListener('connectionstatechange', changed);
            pc.removeEventListener('iceconnectionstatechange', changed);
            channel.removeEventListener('open', changed);
            channel.removeEventListener('close', changed);
            channel.removeEventListener('error', failed);
          };
          const failed = () => { cleanup(); reject(Error('datachannel-connection-failed')); };
          const changed = () => {
            if (pc.connectionState === 'connected' &&
                ['connected', 'completed'].includes(pc.iceConnectionState) && channel.readyState === 'open') {
              cleanup(); resolve();
            } else if (pc.connectionState === 'failed' || pc.connectionState === 'closed' ||
                pc.iceConnectionState === 'failed' || channel.readyState === 'closed') failed();
          };
          timer = setTimeout(() => { cleanup(); reject(Error('datachannel-connection-timeout')); }, options.timeoutMs);
          pc.addEventListener('connectionstatechange', changed);
          pc.addEventListener('iceconnectionstatechange', changed);
          channel.addEventListener('open', changed);
          channel.addEventListener('close', changed);
          channel.addEventListener('error', failed);
          changed();
        });
      };
      const signaler = createRoomWebRtcSignaler({url: options.url, roomId: options.roomId,
        role: options.role, timeoutMs: options.timeoutMs,
        createOffer: async () => {
          const offer = await pc.createOffer();
          await pc.setLocalDescription(offer);
          await waitForIce();
          checkHostCandidates(pc.localDescription, 'local_candidate_types');
          return {type: pc.localDescription.type, sdp: pc.localDescription.sdp};
        },
        acceptOffer: async offer => {
          await pc.setRemoteDescription(offer);
          checkHostCandidates(pc.remoteDescription, 'remote_candidate_types');
          const answer = await pc.createAnswer();
          await pc.setLocalDescription(answer);
          await waitForIce();
          checkHostCandidates(pc.localDescription, 'local_candidate_types');
          return {type: pc.localDescription.type, sdp: pc.localDescription.sdp};
        },
        acceptAnswer: async answer => {
          await pc.setRemoteDescription(answer);
          checkHostCandidates(pc.remoteDescription, 'remote_candidate_types');
        }});
      state.signaler = signaler;
      await signaler.negotiated;
      await waitForConnected();
      signaler.assertHealthy();
      return {peer: window.__netPeer.snapshot(), signaling: signaler.snapshot(),
        local_webrtc: {role: state.role, attach_source: state.attach_source,
          ready_state_at_attach: state.ready_state_at_attach, ordered: state.channel?.ordered ?? null,
          max_retransmits: state.channel?.maxRetransmits ?? null,
          max_packet_lifetime: state.channel?.maxPacketLifeTime ?? null,
          connection_state: pc.connectionState, ice_connection_state: pc.iceConnectionState,
          local_candidate_types: state.local_candidate_types,
          remote_candidate_types: state.remote_candidate_types}};
      }, pageOptions));
    };
    instance.assertRoomSignalingHealthy = () => bounded(() => page.evaluate(() => {
      const signaler = window.__meleeLocalWebRtc?.signaler;
      if (!signaler) throw Error('Room WebRTC signaling is unavailable');
      signaler.assertHealthy();
      return signaler.snapshot();
    }));
    instance.createLocalWebRtcOffer = () => bounded(() => page.evaluate(async timeoutMs => {
      if (window.__meleeLocalWebRtc) throw Error('Local WebRTC peer connection was already created');
      const pc = new RTCPeerConnection({iceServers: []});
      const channel = pc.createDataChannel('a3-native-input', {ordered: true});
      const state = window.__meleeLocalWebRtc = {role: 'alpha', pc, channel,
        attach_source: 'createDataChannel', ready_state_at_attach: channel.readyState,
        attach_error: null, peerCreated: null};
      const peer = window.__meleeCreateBrowserNativePeerForChannel(channel);
      const waitForIce = async () => {
        if (pc.iceGatheringState === 'complete') return;
        await new Promise((resolve, reject) => {
          const done = error => {
            clearTimeout(timer);
            pc.removeEventListener('icegatheringstatechange', changed);
            if (error) reject(error); else resolve();
          };
          const changed = () => { if (pc.iceGatheringState === 'complete') done(); };
          const timer = setTimeout(() => done(Error('Local WebRTC ICE gathering timed out')), timeoutMs);
          pc.addEventListener('icegatheringstatechange', changed);
          changed();
        });
      };
      const offer = await pc.createOffer();
      await pc.setLocalDescription(offer);
      await waitForIce();
      const candidates = pc.localDescription.sdp.split(/\r?\n/)
        .filter(line => line.startsWith('a=candidate:')).map(line => line.trim().split(/\s+/)[7]);
      return {peer, description: {type: pc.localDescription.type, sdp: pc.localDescription.sdp},
        candidate_types: candidates};
    }, Math.max(1, Math.floor(remaining()))));
    instance.acceptLocalWebRtcOffer = offer => bounded(() => page.evaluate(async ({offer, timeoutMs}) => {
      const state = window.__meleeLocalWebRtc;
      if (state?.role !== 'beta' || !state.pc) throw Error('Local WebRTC beta receiver is not prepared');
      await state.pc.setRemoteDescription(offer);
      const answer = await state.pc.createAnswer();
      await state.pc.setLocalDescription(answer);
      if (state.pc.iceGatheringState !== 'complete') {
        await new Promise((resolve, reject) => {
          const done = error => {
            clearTimeout(timer);
            state.pc.removeEventListener('icegatheringstatechange', changed);
            if (error) reject(error); else resolve();
          };
          const changed = () => { if (state.pc.iceGatheringState === 'complete') done(); };
          const timer = setTimeout(() => done(Error('Local WebRTC ICE gathering timed out')), timeoutMs);
          state.pc.addEventListener('icegatheringstatechange', changed);
          changed();
        });
      }
      const candidates = state.pc.localDescription.sdp.split(/\r?\n/)
        .filter(line => line.startsWith('a=candidate:')).map(line => line.trim().split(/\s+/)[7]);
      return {description: {type: state.pc.localDescription.type, sdp: state.pc.localDescription.sdp},
        candidate_types: candidates};
    }, {offer, timeoutMs: Math.max(1, Math.floor(remaining()))}));
    instance.acceptLocalWebRtcAnswer = answer => bounded(() => page.evaluate(async answer => {
      const state = window.__meleeLocalWebRtc;
      if (state?.role !== 'alpha' || !state.pc) throw Error('Local WebRTC alpha peer is not prepared');
      await state.pc.setRemoteDescription(answer);
      return true;
    }, answer));
    instance.waitForLocalWebRtcPeer = () => bounded(() => page.evaluate(async timeoutMs => {
      const state = window.__meleeLocalWebRtc;
      if (!state) throw Error('Local WebRTC peer connection was not prepared');
      if (state.role === 'alpha') return window.__netPeer?.snapshot() ?? null;
      let timer;
      try {
        return await Promise.race([state.peerCreated, new Promise((_, reject) => {
          timer = setTimeout(() => reject(Error('Local WebRTC datachannel event did not create the beta peer')), timeoutMs);
        })]);
      } finally { clearTimeout(timer); }
    }, Math.max(1, Math.floor(remaining()))));
    instance.localWebRtcState = () => bounded(() => page.evaluate(() => {
      const state = window.__meleeLocalWebRtc;
      if (!state) return null;
      const {pc, channel} = state;
      const candidateTypes = description => description?.sdp?.split(/\r?\n/)
        .filter(line => line.startsWith('a=candidate:')).map(line => line.trim().split(/\s+/)[7]) ?? [];
      return {role: state.role, attach_source: state.attach_source,
        ready_state_at_attach: state.ready_state_at_attach, attach_error: state.attach_error,
        ready_state: channel?.readyState ?? null, ordered: channel?.ordered ?? null,
        max_retransmits: channel?.maxRetransmits ?? null,
        max_packet_lifetime: channel?.maxPacketLifeTime ?? null,
        connection_state: pc.connectionState, ice_connection_state: pc.iceConnectionState,
        local_candidate_types: candidateTypes(pc.localDescription),
        remote_candidate_types: candidateTypes(pc.remoteDescription),
        room_signaling: state.signaler?.snapshot() ?? null,
        room_signaling_error: state.signaler?.snapshot().failure_code ?? null};
    }));
    instance.closeLocalWebRtc = () => bounded(() => page.evaluate(async () => {
      const state = window.__meleeLocalWebRtc;
      if (!state) return null;
      state.signaler?.armClose();
      const results = await Promise.allSettled([
        state.signaler ? state.signaler.close() : Promise.resolve(null),
        Promise.resolve().then(() => {
          if (state.pc.connectionState !== 'closed') state.pc.close();
        }),
      ]);
      const failures = results.filter(result => result.status === 'rejected').map(result => result.reason);
      if (failures.length) throw new AggregateError(failures, 'Local WebRTC cleanup failed');
      return {role: state.role, connection_state: state.pc.connectionState,
        channel_state: state.channel?.readyState ?? null,
        room_signaling: state.signaler?.snapshot() ?? null};
    }));
    instance.readPeerSnapshot = () => bounded(() => page.evaluate(() => {
      if (typeof window.__netPeer?.snapshot === 'function') return window.__netPeer.snapshot();
      return window.meleeNetRuntimeLockstepSnapshot?.()?.peer ?? null;
    }));
    instance.runtimeLockstepSnapshot = () => bounded(() => page.evaluate(() =>
      window.meleeNetRuntimeLockstepSnapshot?.() ?? null));
    instance.configureRuntimeLockstep = options => {
      if (!runtimeOwned) throw Error('Runtime-owned lockstep config is unavailable for this browser owner');
      return bounded(() => page.evaluate(options => window.meleeNetConfigureRuntimeLockstep(options), options));
    };
    instance.closeRuntimeLockstep = mode => {
      if (!runtimeOwned) throw Error('Runtime-owned lockstep close is unavailable for this browser owner');
      return bounded(() => page.evaluate(mode => window.meleeNetCloseRuntimeLockstep(mode), mode));
    };
    instance.peerRpc = (name, args = []) => bounded(() => page.evaluate(([name, args]) =>
      window.__netPeer.rpc(name, args), [name, args]));
    instance.armPeerClose = () => bounded(() => page.evaluate(() => {
      window.__netPeer?.armClose();
      window.__meleeLocalWebRtc?.signaler?.armClose();
      return true;
    }));
    instance.closePeer = intentional => bounded(() => page.evaluate(intentional =>
      window.__netPeer?.close({intentional}), intentional));
    instance.freezeLoadedWasmIdentity = () => bounded(() => wasmResponses.freeze());
    const response = await bounded(() => page.goto(url, {waitUntil: 'domcontentloaded'}));
    if (response?.status() !== 200) throw Error(`runtime.html returned HTTP ${response?.status()}`);
    const headers = response.headers();
    if (headers['cross-origin-opener-policy'] !== 'same-origin' || headers['cross-origin-embedder-policy'] !== 'require-corp')
      throw Error('Runtime did not load over COOP/COEP HTTP isolation');
    if (!await bounded(() => page.evaluate(() => crossOriginIsolated))) throw Error('Browser page is not cross-origin isolated');
    await driver.waitForImport();
    if (runtimeOwned) {
      await bounded(() => page.evaluate(() => {
        if (window.__net) throw Error('Runtime-owned native observation facade already exists');
        const status = () => {
          const pointer = Module._melee_web_net_status();
          return JSON.parse(Module.UTF8ToString(pointer));
        };
        window.__net = {
          status,
          native() {
            return {phase: Module._melee_web_native_menu_phase(),
              running: Module._melee_web_native_menu_running(),
              message: Module.UTF8ToString(Module._melee_web_native_menu_message()),
              error: document.querySelector('#status')?.dataset.runtimeError || null,
              status: document.querySelector('#status')?.textContent?.slice(-300) || null};
          },
          renderSource() { return Module.UTF8ToString(Module._melee_web_native_menu_diagnostics()); },
        };
      }));
    } else await bounded(() => page.evaluate(PAGE_HELPERS));
    if (throttle !== 1) {
      instance.cdp = await bounded(() => context.newCDPSession(page));
      await bounded(() => instance.cdp.send('Emulation.setCPUThrottlingRate', {rate: throttle}));
    }
    // The arena pattern is a pre-session diagnostic; it must precede disc import.
    if (arenaFill >= 0) {
      const accepted = await bounded(() => page.evaluate(pattern => Module._melee_web_net_arena_fill(pattern), arenaFill));
      if (!accepted) throw Error('The session arena pattern was rejected');
    }
    instance.userAgent = await bounded(() => page.evaluate(() => navigator.userAgent));
    instance.browserVersion = context.browser()?.version() ?? null;
    instance.importDisc = async () => { await driver.selectDisc(disc); await driver.waitForStart(); };
    instance.prepareSyntheticGamepadRouting = localPort => {
      if (!syntheticGamepad) throw Error('Synthetic Gamepad routing requires its input fixture');
      return bounded(() => page.evaluate(({localPort, gamepadIndex}) => {
        const manager = Module?.meleeControllers;
        if (!manager || typeof manager.inspect !== 'function' || typeof manager.assign !== 'function')
          throw Error('The existing browser controller manager is unavailable');
        const discovered = manager.inspect();
        if (discovered.length !== 1 || discovered[0].index !== gamepadIndex)
          throw Error(`Expected only synthetic Gamepad ${gamepadIndex} after neutral discovery`);
        const row = discovered[0];
        const automaticPort = row.port;
        manager.assign(row.key, localPort);
        const routed = manager.inspect().find(candidate => candidate.key === row.key);
        const output = routed?.output;
        if (!routed || routed.port !== localPort || !routed.active || routed.status !== 'ready' ||
            output?.buttons !== 0 || [...(output?.stick || []), ...(output?.cstick || []),
              ...(output?.triggers || [])].some(value => value !== 0))
          throw Error(`Synthetic Gamepad ${gamepadIndex} did not activate neutrally on local port ${localPort}`);
        return {gamepad_index: gamepadIndex, automatic_port: automaticPort,
          assigned_port: routed.port, active: routed.active, neutral: true};
      }, {localPort, gamepadIndex: syntheticGamepad.index}));
    };
    instance.begin = (seed, maxFrames) => bounded(() => page.evaluate(([s, m]) => window.meleeNetBegin(s, m), [seed >>> 0, maxFrames]));
    instance.beginLockstep = (seed, maxFrames) => bounded(() => page.evaluate(([s, m]) => window.meleeNetBeginLockstep(s, m), [seed >>> 0, maxFrames]));
    instance.peerIdentity = () => bounded(() => page.evaluate(() => window.meleeNetPeerIdentity()));
    instance.push = async frames => {
      if (frames.length % NET_FRAME_BYTES) throw Error('Networked frames must be 44-byte multiples');
      const ok = await bounded(() => page.evaluate(base64 => window.__net.push(base64), Buffer.from(frames).toString('base64')));
      if (!ok) throw Error('The native queue rejected an agreed frame chunk');
    };
    instance.pushIndexed = async (firstTick, frames) => {
      if (frames.length % NET_FRAME_BYTES) throw Error('Lockstep frames must be 44-byte multiples');
      const ok = await bounded(() => page.evaluate(([tick, base64]) => window.__net.pushIndexed(tick, base64),
        [firstTick, Buffer.from(frames).toString('base64')]));
      if (!ok) throw Error(`The native queue rejected indexed frames beginning at ${firstTick}`);
    };
    instance.confirmStart = () => bounded(() => page.evaluate(() => window.__net.confirmStart()));
    instance.terminate = (kind, tick, channel = 0) => bounded(() => page.evaluate(([k, t, c]) => {
      window.__net.terminate(k, t, c); return window.__net.status();
    }, [kind, tick, channel]));
    instance.drain = async (max = 1024) => {
      if (browserPeerAllocated) throw Error('The browser runtime owner solely owns the native checksum drain');
      const result = await bounded(() => page.evaluate(count => window.__net.drain(count), max));
      return {count: result.count, bytes: Buffer.from(result.data, 'base64')};
    };
    instance.status = () => bounded(() => page.evaluate(() => window.__net.status()));
    instance.installSourceAccounting = options => bounded(() => installNetSourceAccounting(page, 32768, options));
    instance.readSourceAccounting = options => bounded(() => readNetSourceAccounting(page, options));
    instance.native = () => bounded(() => page.evaluate(() => window.__net.native()));
    instance.observe = () => bounded(() => page.evaluate(() => window.__net.observe()));
    instance.graphics = () => bounded(() => page.evaluate(async () => {
      const canvas = document.querySelector('canvas');
      const adapter = globalThis.navigator.gpu ? await navigator.gpu.requestAdapter().catch(() => null) : null;
      let webgl = null;
      const context = canvas?.getContext('webgl2') || canvas?.getContext('webgl');
      if (context) {
        const debug = context.getExtension('WEBGL_debug_renderer_info');
        webgl = {
          version: context.getParameter(context.VERSION),
          renderer: context.getParameter(debug ? debug.UNMASKED_RENDERER_WEBGL : context.RENDERER),
        };
      }
      return {cross_origin_isolated: crossOriginIsolated, webgpu_api: !!navigator.gpu,
        webgpu_adapter: !!adapter,
        canvas: canvas ? {width: canvas.width, height: canvas.height} : null, webgl};
    }));
    instance.screenshot = file => bounded(() => page.screenshot({path: file, fullPage: false}));
    instance.maybeResume = async cursor => {
      const resumed = await bounded(() => page.evaluate(() => window.__net.resumeTimingPause()));
      if (resumed) instance.timingResumes.push({cursor, at_ms: Date.now()});
      return resumed;
    };
    instance.timingPauseDiagnostics = () => bounded(() => page.evaluate(() =>
      window.meleeNetTimingPauseDiagnostics?.() ?? null));
    instance.unload = () => driver.unload();
    return instance;
  } catch (error) {
    if (error && typeof error === 'object') {
      error.browserErrors = instance?.errors ? [...instance.errors] : [];
      error.startupDiagnostics = error.diagnostics ?? null;
    }
    let browserClosed = false;
    try { browserClosed = await close(); }
    catch (cleanupError) {
      if (error && typeof error === 'object') error.cleanupError = cleanupError;
    }
    if (error && typeof error === 'object') error.browserClosed = browserClosed || closeComplete;
    throw error;
  }
}

/** Recipe-facing facade. Cached fields are only for decisions immediately after
 * refreshBrowserPeers(); the page snapshot is the authoritative evidence. */
export function browserPeerFacade(instance, initial) {
  let snapshot = initial;
  const update = row => { snapshot = row; return row; };
  const serialize = value => ArrayBuffer.isView(value) ? Array.from(value) :
    Array.isArray(value) ? value.map(serialize) : value && typeof value === 'object' ?
      Object.fromEntries(Object.entries(value).map(([key, child]) => [key, serialize(child)])) : value;
  const invoke = async (name, args) => update(await instance.peerRpc(name, serialize(args)));
  const facade = {
    browserOwned: true,
    refresh: () => invoke('snapshot', []),
    start: () => invoke('start', []),
    addLocalInput: (...args) => invoke('addLocalInput', args),
    addLocalInputs: (...args) => invoke('addLocalInputs', args),
    releaseCapturedInput: tick => invoke('releaseCapturedInput', [tick]),
    setNativeProgress: (...args) => invoke('setNativeProgress', args),
    disconnect: (...args) => invoke('disconnect', args),
    fail: (...args) => invoke('fail', args),
    async drain() {
      const row = await invoke('drain', []);
      return {count: row.records.length, bytes: Buffer.concat(row.records.map(record => Buffer.from(record)))};
    },
    close: intentional => instance.closePeer(intentional).then(update),
    armClose: () => instance.armPeerClose(),
    summary: () => snapshot.protocol,
    get errors() { return snapshot.endpointErrors; },
    get transport() { return snapshot.transport; },
    get checksumOwnership() { return snapshot.checksumOwnership; },
    get localInputCapture() { return snapshot.localInputCapture; },
  };
  for (const [name, field] of Object.entries({ready: 'ready', terminal: 'terminal',
    remoteAckInput: 'remote_ack_input', inputDuplicates: 'input_duplicates',
    outOfOrderInputs: 'out_of_order_inputs', checksumMismatches: 'checksum_mismatches',
    agreementHash: 'start_identity_hash'}))
    Object.defineProperty(facade, name, {get: () => snapshot.protocol[field]});
  return facade;
}

export function createPeerModuleResponseObserver({url, peerModuleHashes, runtimeArtifactNames, onFailure = () => {}}) {
  const relayModules = ['net_lockstep_browser_peer.mjs', 'net_lockstep_core.mjs',
    'net_lockstep_native_adapter.mjs', 'net_lockstep_websocket_relay.mjs'];
  const webrtcModules = [...relayModules, 'net_lockstep_webrtc.mjs'];
  const roomSignaledWebRtcModules = [...webrtcModules, 'net_lockstep_webrtc_signaling.mjs'];
  const runtimeOwnedModules = [...roomSignaledWebRtcModules,
    'net_lockstep_runtime_owner.mjs', 'net_lockstep_development_owner.mjs'];
  const provided = Object.keys(peerModuleHashes).sort().join();
  const names = [relayModules, webrtcModules, roomSignaledWebRtcModules, runtimeOwnedModules]
    .find(modules => provided === [...modules].sort().join());
  if (!names ||
      Object.values(peerModuleHashes).some(hash => !/^[0-9a-f]{64}$/.test(hash)))
    throw Error('Browser peer requires an exact relay, WebRTC, room-signaling or runtime-owned module SHA-256 inventory');
  const expected = new Map(names.map(name => [new URL(name, url).href, peerModuleHashes[name]]));
  const allowed = new Set([...runtimeArtifactNames, ...names].map(name => new URL(name, url).href));
  const responses = [], tasks = new Set();
  let failure = null, eventCount = 0, browserClosed = false;
  const fail = error => {
    if (failure) return;
    failure = error;
    onFailure(error);
  };
  function observe(response) {
    const responseUrl = response.url();
    if (!new URL(responseUrl).pathname.endsWith('.mjs')) return;
    if (failure) return;
    if (++eventCount > allowed.size) { fail(Error('Browser module response event bound exceeded')); return; }
    const task = (async () => {
      if (!allowed.has(responseUrl)) throw Error(`Unexpected browser module import: ${responseUrl}`);
      if (!expected.has(responseUrl)) return;
      const headers = await response.allHeaders();
      if (browserClosed) throw Error('Browser closed before peer module response body observation');
      const bytes = await response.body();
      if (bytes.length > 1024 * 1024) throw Error('Browser peer module response exceeded its 1 MiB bound');
      const hash = createHash('sha256').update(bytes).digest('hex');
      if (response.status() !== 200 || hash !== expected.get(responseUrl) ||
          headers['cross-origin-opener-policy'] !== 'same-origin' ||
          headers['cross-origin-embedder-policy'] !== 'require-corp')
        throw Error(`Browser peer module response identity differs: ${responseUrl}`);
      if (responses.some(row => row.url === responseUrl)) throw Error('Browser peer module response inventory is incomplete or duplicated');
      responses.push({url: responseUrl, status: response.status(), bytes: bytes.length, sha256: hash,
        coop: headers['cross-origin-opener-policy'], coep: headers['cross-origin-embedder-policy']});
    })().catch(fail).finally(() => tasks.delete(task));
    tasks.add(task);
  }
  async function freeze({closed = false} = {}) {
    browserClosed ||= closed;
    while (tasks.size) await Promise.all([...tasks]);
    if (failure) throw failure;
    if (responses.length !== expected.size || [...expected.keys()].some(url =>
      responses.filter(row => row.url === url).length !== 1))
      throw Error('Browser peer module response inventory is incomplete or duplicated');
    return [...responses];
  }
  return {observe, freeze};
}
