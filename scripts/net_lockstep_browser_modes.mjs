// Validate transport scope before the harness reads inputs or creates output.
export function validateLockstepBrowserMode(values) {
  const scenario = values.scenario;
  if (!['probe', 'positive', 'flip', 'disconnect', 'input-sampling', 'native-pump'].includes(scenario))
    throw Error('--scenario must be probe, positive, flip, disconnect, input-sampling, or native-pump');
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
  if (runtimeOwned && (scenario !== 'native-pump' || !localWebRtc || values['webrtc-signaling'] !== 'room-worker'))
    throw Error('Runtime-owned peers require native-pump with WebRTC and Room Worker signaling');
  if (!localWebRtc && values['webrtc-signaling'] !== 'memory')
    throw Error('--webrtc-signaling applies only to the local WebRTC transport');
  if (browserOwned && !localWebRtc && !values['relay-url'])
    throw Error('Browser-owned relay peers require --relay-url');
  if (localWebRtc && (!browserOwned || !['input-sampling', 'positive', 'disconnect', 'flip', 'native-pump'].includes(scenario)))
    throw Error('The local WebRTC endpoint requires browser-owned input-sampling, positive, disconnect, flip, or native-pump mode');
  if (localWebRtc && ['positive', 'disconnect', 'flip', 'native-pump'].includes(scenario) && !roomWorkerSignaling)
    throw Error(`${scenario[0].toUpperCase() + scenario.slice(1)} WebRTC mode requires --webrtc-signaling room-worker`);
  if (scenario === 'native-pump' && (!browserOwned || !localWebRtc || !roomWorkerSignaling ||
      Number(values['source-ticks']) !== 8))
    throw Error('Native-pump diagnostic requires browser-owned Room Worker WebRTC and exactly eight source ticks');
  if (scenario === 'input-sampling' && !browserOwned)
    throw Error('The input-sampling scenario requires --peer-owner browser');
  const runtimeInputFixture = values['runtime-input-fixture'] !== undefined;
  if (runtimeInputFixture && (values['runtime-input-fixture'] !== 'neutral-a-release' || !runtimeOwned || scenario !== 'native-pump'))
    throw Error('--runtime-input-fixture requires neutral-a-release with runtime-owned eight-CSS native-pump');
  return {browserOwned, runtimeOwned, peerTransport, localWebRtc, roomWorkerSignaling, ...(runtimeInputFixture ? {runtimeInputFixture} : {})};
}
