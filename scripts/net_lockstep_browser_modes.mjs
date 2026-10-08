// Validate transport scope before the harness reads inputs or creates output.
export function validateLockstepBrowserMode(values) {
  const scenario = values.scenario;
  if (!['probe', 'positive', 'flip', 'disconnect', 'input-sampling', 'native-pump'].includes(scenario))
    throw Error('--scenario must be probe, positive, flip, disconnect, input-sampling, or native-pump');
  if (!['node', 'browser'].includes(values['peer-owner']))
    throw Error('--peer-owner must be node or browser');
  const browserOwned = values['peer-owner'] === 'browser';
  const peerTransport = values['peer-transport'] ??
    (browserOwned || values['relay-url'] ? 'relay' : 'tcp-loopback');
  if (values['peer-transport'] !== undefined && !['relay', 'webrtc'].includes(peerTransport))
    throw Error('--peer-transport must be relay or webrtc');
  if (!browserOwned && values['peer-transport'] !== undefined)
    throw Error('--peer-transport applies only to browser-owned peers');
  const localWebRtc = peerTransport === 'webrtc';
  const roomWorkerSignaling = values['webrtc-signaling'] === 'room-worker';
  if (!['memory', 'room-worker'].includes(values['webrtc-signaling']))
    throw Error('--webrtc-signaling must be memory or room-worker');
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
  return {browserOwned, peerTransport, localWebRtc, roomWorkerSignaling};
}
