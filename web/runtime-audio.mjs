/** Development Web Audio transport; excluded from the audio-disabled public alpha. */
export function createRuntimeAudio({assetBase, onEvent, onError, onFatal}) {
  let context = null, node = null, enabled = false, acknowledged = true;
  const waiters = [];
  const waitForAck = () => acknowledged ? Promise.resolve() : new Promise((resolve, reject) => {
    const waiter = {resolve, reject, timer: setTimeout(() =>
      onFatal(Error('Audio did not acknowledge preparation. Reload to recover.')), 10000)};
    waiters.push(waiter);
  });
  const setEnabled = value => {
    if (value !== enabled) {
      enabled = value; acknowledged = !node;
      node?.port.postMessage({type: 'state', enabled});
    }
  };
  return Object.freeze({
    async prepare() {
      if (!context) {
        context = new AudioContext({sampleRate: 32000, latencyHint: 'interactive'});
        if (context.sampleRate !== 32000) throw Error('Expected 32000 Hz audio context.');
        await context.audioWorklet.addModule(new URL('audio-worklet.js', assetBase).href);
        node = new AudioWorkletNode(context, 'melee-audio-output', {
          numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2],
        });
        acknowledged = false;
        node.port.onmessage = ({data}) => {
          if (data.type === 'state-ack' && data.enabled === enabled) {
            acknowledged = true;
            for (const waiter of waiters.splice(0)) { clearTimeout(waiter.timer); waiter.resolve(); }
          }
          if (data.error) onError(Error(data.error));
          onEvent(data);
        };
        node.port.postMessage({type: 'state', enabled: false});
        node.connect(context.destination);
      }
      await context.resume();
    },
    setEnabled,
    async pause() { setEnabled(false); await waitForAck(); },
    waitForAck,
    readyForPreparation: () => !node || (!enabled && acknowledged),
    write(pcm) { if (enabled) node?.port.postMessage({type: 'pcm', pcm}, [pcm.buffer]); },
    fail(error) {
      for (const waiter of waiters.splice(0)) { clearTimeout(waiter.timer); waiter.reject(error); }
    },
    async destroy() { node?.disconnect(); if (context) await context.close(); },
  });
}
