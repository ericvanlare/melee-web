// Validate transport scope before the harness reads inputs or creates output.
import {createHash} from 'node:crypto';
import {BUTTONS, STANDARD_PROFILE, normalizeController} from '../web/controller-input.mjs';
import {LOCKSTEP_MAX_SOURCE_TICKS, lockstepConstants} from './net_lockstep_core.mjs';

export const RUNTIME_CSS_SSS_INPUT_TICKS = 518;
export const RUNTIME_CSS_SSS_SOURCE_TICKS = 520;
export const RUNTIME_CSS_SSS_STIMULUS_FRAMES = 153;
export const RUNTIME_CSS_SSS_GENERATOR_SHA256 = 'eec74da2d353a16df177d3b5da2dfd5ef905687f53f02351cf47a0850d2e42ca';
const MWNI_HEADER_BYTES = 16;
const MWNI_FRAME_BYTES = 44;
const MWNI_PORT_BYTES = 11;

const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');

/** Validate all external route provenance before the harness creates output or starts a browser. */
export function validateRuntimeCssSssRecipe({scriptBytes, manifestBytes, generatorBytes, seed}) {
  const script = Buffer.from(scriptBytes ?? []);
  const manifestSource = Buffer.from(manifestBytes ?? []);
  const generator = Buffer.from(generatorBytes ?? []);
  if (sha256(generator) !== RUNTIME_CSS_SSS_GENERATOR_SHA256)
    throw Error('CSS-to-SSS input recipe generator identity is not the reviewed version');
  if (script.length < MWNI_HEADER_BYTES || script.subarray(0, 4).toString() !== 'MWNI' ||
      script.readUInt32BE(4) !== 1 || script.readUInt32BE(12) !== 0)
    throw Error('CSS-to-SSS recipe is not a valid MWNI v1 file');
  const frameCount = script.readUInt32BE(8);
  const body = script.subarray(MWNI_HEADER_BYTES);
  if (frameCount < RUNTIME_CSS_SSS_INPUT_TICKS)
    throw Error('CSS-to-SSS MWNI frame count is below the selected input bound');
  if (frameCount > LOCKSTEP_MAX_SOURCE_TICKS)
    throw Error(`CSS-to-SSS MWNI frame count exceeds the ${LOCKSTEP_MAX_SOURCE_TICKS}-frame source bound`);
  if (script.length !== MWNI_HEADER_BYTES + frameCount * MWNI_FRAME_BYTES)
    throw Error('CSS-to-SSS MWNI body length is invalid');
  let manifest;
  try { manifest = JSON.parse(manifestSource.toString('utf8')); }
  catch { throw Error('CSS-to-SSS recipe manifest is not valid JSON'); }
  const scriptSha256 = sha256(script);
  const bodySha256 = sha256(body);
  if (manifest?.schema !== 'melee-web-net-input-script-v1' || manifest.version !== 1 ||
      manifest.frame_bytes !== MWNI_FRAME_BYTES || manifest.frames !== frameCount ||
      manifest.seed !== seed || !Array.isArray(manifest.human_ports) ||
      JSON.stringify(manifest.human_ports) !== '[0,1]' ||
      manifest.frames_sha256 !== bodySha256 || manifest.script_sha256 !== scriptSha256)
    throw Error('CSS-to-SSS recipe manifest does not bind the script format, count, seed and digests');
  return Object.freeze({generator_sha256: RUNTIME_CSS_SSS_GENERATOR_SHA256,
    script_sha256: scriptSha256, manifest_sha256: sha256(manifestSource), frame_count: frameCount,
    stimulus_frames: RUNTIME_CSS_SSS_STIMULUS_FRAMES, seed});
}

/** Convert the reviewed recipe's CSS prefix into ordinary standard Gamepad states. */
export function buildRuntimeCssSssGamepadSamples(scriptFrames) {
  const frames = Buffer.from(scriptFrames ?? []);
  const prefixBytes = RUNTIME_CSS_SSS_STIMULUS_FRAMES * MWNI_FRAME_BYTES;
  if (frames.length < prefixBytes)
    throw Error('CSS-to-SSS recipe is shorter than its 153-frame stimulus prefix');
  const noControllerPad = Buffer.from(lockstepConstants.noControllerPad, 'hex');
  for (let inputTick = 0; inputTick < RUNTIME_CSS_SSS_STIMULUS_FRAMES; ++inputTick) {
    for (const port of [2, 3]) {
      const offset = inputTick * MWNI_FRAME_BYTES + port * MWNI_PORT_BYTES;
      if (!frames.subarray(offset, offset + MWNI_PORT_BYTES).equals(noControllerPad))
        throw Error(`CSS-to-SSS recipe input ${inputTick} port ${port} is not the canonical no-controller PAD`);
    }
  }
  const byRole = {};
  for (const [role, port] of [['alpha', 0], ['beta', 1]]) {
    const samples = [];
    for (let inputTick = 0; inputTick < RUNTIME_CSS_SSS_INPUT_TICKS; ++inputTick) {
      const bytes = inputTick < RUNTIME_CSS_SSS_STIMULUS_FRAMES ?
        Buffer.from(frames.subarray(inputTick * MWNI_FRAME_BYTES + port * MWNI_PORT_BYTES,
          inputTick * MWNI_FRAME_BYTES + (port + 1) * MWNI_PORT_BYTES)) : Buffer.alloc(MWNI_PORT_BYTES);
      if (inputTick === 0 && bytes.some(byte => byte !== 0))
        throw Error(`CSS-to-SSS first selected sample for ${role} must be neutral`);
      if (bytes.length !== MWNI_PORT_BYTES || bytes[8] !== 0 || bytes[9] !== 0 || bytes.readInt8(10) !== 0 ||
          bytes.readInt8(4) !== 0 || bytes.readInt8(5) !== 0 || bytes[6] !== 0 || bytes[7] !== 0)
        throw Error(`CSS-to-SSS recipe input ${inputTick} port ${port} has unsupported PAD fields`);
      const buttons = bytes.readUInt16BE(0);
      const expectedButtons = role === 'alpha' && inputTick >= 150 && inputTick <= 152 ? BUTTONS.Start : 0;
      if (buttons !== expectedButtons)
        throw Error(`CSS-to-SSS recipe input ${inputTick} port ${port} has unsupported buttons`);
      const stickX = bytes.readInt8(2), stickY = bytes.readInt8(3);
      const gamepad = {buttons: Array.from({length: 17}, (_, index) => ({
        pressed: index === 9 && (buttons & BUTTONS.Start) !== 0,
        value: index === 9 && (buttons & BUTTONS.Start) !== 0 ? 1 : 0,
      })), axes: [stickX / 128, stickY === 0 ? 0 : -stickY / 128, 0, 0]};
      const normalized = normalizeController(gamepad, STANDARD_PROFILE);
      if (normalized.buttons !== buttons || normalized.stick[0] !== stickX || normalized.stick[1] !== stickY ||
          normalized.cstick.some(value => value !== 0) || normalized.triggers.some(value => value !== 0))
        throw Error(`CSS-to-SSS recipe input ${inputTick} port ${port} does not round-trip through the standard controller profile`);
      samples.push({input_tick: inputTick, bytes: Array.from(bytes),
        gamepad: {kind: 'css-start-to-sss', input_tick: inputTick,
          buttons: gamepad.buttons.map(button => ({...button})), axes: [...gamepad.axes]}});
    }
    byRole[role] = samples;
  }
  return byRole;
}

export function validateLockstepBrowserMode(values) {
  const scenario = values.scenario;
  if (!['probe', 'positive', 'flip', 'disconnect', 'input-sampling', 'native-pump', 'runtime-css-sss'].includes(scenario))
    throw Error('--scenario must be probe, positive, flip, disconnect, input-sampling, native-pump, or runtime-css-sss');
  if (scenario !== 'runtime-css-sss' && values['script-manifest'] !== undefined)
    throw Error('--script-manifest applies only to runtime-css-sss');
  if (!['node', 'browser', 'runtime'].includes(values['peer-owner']))
    throw Error('--peer-owner must be node, browser or runtime');
  const runtimeOwned = values['peer-owner'] === 'runtime';
  const browserOwned = values['peer-owner'] !== 'node';
  const peerTransport = values['peer-transport'] ??
    (runtimeOwned ? 'webrtc' : browserOwned || values['relay-url'] ? 'relay' : 'tcp-loopback');
  if (values['peer-transport'] !== undefined && !['relay', 'webrtc'].includes(peerTransport))
    throw Error('--peer-transport must be relay or webrtc');
  if (!browserOwned && values['peer-transport'] !== undefined)
    throw Error('--peer-transport applies only to browser-owned peers');
  const localWebRtc = peerTransport === 'webrtc';
  const roomWorkerSignaling = values['webrtc-signaling'] === 'room-worker';
  if (!['memory', 'room-worker'].includes(values['webrtc-signaling']))
    throw Error('--webrtc-signaling must be memory or room-worker');
  if (runtimeOwned && (!['native-pump', 'runtime-css-sss'].includes(scenario) || !localWebRtc || values['webrtc-signaling'] !== 'room-worker'))
    throw Error('Runtime-owned peers require native-pump or runtime-css-sss with WebRTC and Room Worker signaling');
  if (!localWebRtc && values['webrtc-signaling'] !== 'memory')
    throw Error('--webrtc-signaling applies only to the local WebRTC transport');
  if (browserOwned && !localWebRtc && !values['relay-url'])
    throw Error('Browser-owned relay peers require --relay-url');
  if (localWebRtc && (!browserOwned || !['input-sampling', 'positive', 'disconnect', 'flip', 'native-pump', 'runtime-css-sss'].includes(scenario)))
    throw Error('The local WebRTC endpoint requires browser-owned input-sampling, positive, disconnect, flip, native-pump, or runtime-css-sss mode');
  if (localWebRtc && ['positive', 'disconnect', 'flip', 'native-pump', 'runtime-css-sss'].includes(scenario) && !roomWorkerSignaling)
    throw Error(`${scenario[0].toUpperCase() + scenario.slice(1)} WebRTC mode requires --webrtc-signaling room-worker`);
  if (scenario === 'native-pump' && (!browserOwned || !localWebRtc || !roomWorkerSignaling ||
      Number(values['source-ticks']) !== 8))
    throw Error('Native-pump diagnostic requires browser-owned Room Worker WebRTC and exactly eight source ticks');
  if (scenario === 'input-sampling' && !browserOwned)
    throw Error('The input-sampling scenario requires --peer-owner browser');
  if (scenario === 'runtime-css-sss' && (!runtimeOwned || !localWebRtc || !roomWorkerSignaling ||
      Number(values['source-ticks']) !== RUNTIME_CSS_SSS_SOURCE_TICKS ||
      !values.script || !values['script-manifest']))
    throw Error('runtime-css-sss requires runtime-owned Room Worker WebRTC, exactly 520 source ticks, --script and --script-manifest');
  const runtimeInputFixture = values['runtime-input-fixture'] !== undefined;
  if (runtimeInputFixture && (values['runtime-input-fixture'] === 'neutral-a-release' ?
      (!runtimeOwned || scenario !== 'native-pump') : values['runtime-input-fixture'] === 'css-start-to-sss' ?
        (!runtimeOwned || scenario !== 'runtime-css-sss') : true))
    throw Error('--runtime-input-fixture must match the selected runtime-owned native-pump or runtime-css-sss mode');
  if (scenario === 'runtime-css-sss' && values['runtime-input-fixture'] !== 'css-start-to-sss')
    throw Error('runtime-css-sss requires --runtime-input-fixture css-start-to-sss');
  return {browserOwned, runtimeOwned, peerTransport, localWebRtc, roomWorkerSignaling,
    ...(runtimeInputFixture ? {runtimeInputFixture: true, runtimeInputFixtureName: values['runtime-input-fixture']} : {})};
}
