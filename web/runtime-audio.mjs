/** Development Web Audio transport; excluded from the audio-disabled public alpha. */
export function createRuntimeAudio({assetBase, onEvent, onError, onFatal}) {
  let context = null, node = null, nodePreparation = null, enabled = false, acknowledged = true;
  const waiters = [];
  let renderSequence = 0;
  const renderWaiters = new Map();
  const clearRenderWaiter = waiter => {
    clearTimeout(waiter.timer);
    clearTimeout(waiter.pollTimer);
  };
  const outputTimingError = cause => {
    const error = Error('Game audio output timing could not be read. Close this message, then choose Play to try again.');
    if (cause !== undefined) error.cause = cause;
    return error;
  };
  const clearRenderWaiters = error => {
    for (const waiter of renderWaiters.values()) { clearRenderWaiter(waiter); waiter.reject(error); }
    renderWaiters.clear();
  };
  const waitForOutputAfter = waiter => {
    const poll = () => {
      if (renderWaiters.get(waiter.id) !== waiter) return;
      if (context.state !== 'running') {
        renderWaiters.delete(waiter.id); clearRenderWaiter(waiter);
        waiter.reject(Error('Game audio stopped before output became ready.'));
        return;
      }
      let timestamp;
      try { timestamp = context.getOutputTimestamp(); }
      catch (error) {
        renderWaiters.delete(waiter.id); clearRenderWaiter(waiter); waiter.reject(outputTimingError(error));
        return;
      }
      if (!Number.isFinite(timestamp?.contextTime) || timestamp.contextTime < 0 ||
          !Number.isFinite(timestamp?.performanceTime) || timestamp.performanceTime < 0) {
        renderWaiters.delete(waiter.id); clearRenderWaiter(waiter);
        waiter.reject(outputTimingError(Error('AudioContext returned an invalid output timestamp.')));
      } else if (timestamp.contextTime > waiter.processTime) {
        renderWaiters.delete(waiter.id); clearRenderWaiter(waiter); waiter.resolve();
      } else waiter.pollTimer = setTimeout(poll, 16);
    };
    poll();
  };
  const waitForAck = () => acknowledged ? Promise.resolve() : new Promise((resolve, reject) => {
    const waiter = {resolve, reject, timer: setTimeout(() =>
      onFatal(Error('Audio did not acknowledge preparation. Reload to recover.')), 10000)};
    waiters.push(waiter);
  });
  const ensureRunning = async resumed => {
    let timer;
    try {
      const complete = await Promise.race([resumed.then(() => true), new Promise(resolve => {
        timer = setTimeout(() => resolve(false), 5000);
      })]);
      if (!complete || context.state !== 'running') {
        throw Error('The browser kept game audio suspended. Close this message, then choose Play to enable audio and start the game.');
      }
    } catch (error) {
      if (context.state !== 'running' && !/choose Play to enable audio/i.test(error?.message || '')) {
        throw Error('The browser kept game audio suspended. Close this message, then choose Play to enable audio and start the game.');
      }
      throw error;
    } finally { clearTimeout(timer); }
  };
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
        context.onstatechange = () => onEvent({type: 'context-state',
          context_state: context.state, audio_clock_seconds: context.currentTime, enabled});
        if (context.sampleRate !== 32000) throw Error('Expected 32000 Hz audio context.');
      }
      // Invoke resume before yielding so the Choose file or recovery Play click
      // can satisfy browsers that require transient user activation for audio.
      const resumed = context.resume();
      if (!node) {
        if (!nodePreparation) {
          nodePreparation = (async () => {
            await context.audioWorklet.addModule(new URL('audio-worklet.js', assetBase).href);
            if (node) return;
            node = new AudioWorkletNode(context, 'melee-audio-output', {
              numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2],
            });
            node.onprocessorerror = () => onFatal(Error('Game audio stopped unexpectedly. Reload to recover.'));
            acknowledged = false;
            node.port.onmessage = ({data}) => {
              if (data.type === 'render-ready') {
                const waiter = renderWaiters.get(data.id);
                if (waiter && waiter.processTime === null) {
                  if (!Number.isFinite(data.process_time) || data.process_time < 0) {
                    renderWaiters.delete(data.id); clearRenderWaiter(waiter);
                    waiter.reject(Error('Audio renderer did not report its process time.'));
                  } else {
                    waiter.processTime = data.process_time;
                    waitForOutputAfter(waiter);
                  }
                }
                return;
              }
              if (data.type === 'state-ack' && data.enabled === enabled) {
                acknowledged = true;
                for (const waiter of waiters.splice(0)) { clearTimeout(waiter.timer); waiter.resolve(); }
              }
              if (data.error) onFatal(Error(data.error));
              onEvent({...data, context_state: context.state,
                audio_clock_seconds: context.currentTime, enabled});
            };
            node.port.postMessage({type: 'state', enabled: false});
            node.connect(context.destination);
          })().finally(() => { nodePreparation = null; });
        }
        await Promise.all([ensureRunning(resumed), nodePreparation]);
      } else {
        await ensureRunning(resumed);
      }
    },
    // Port messages and Worklet callbacks can precede output-device progress.
    // Keep source playback stopped until the device timestamp passes that callback.
    waitForRender() {
      if (!context || !node || context.state !== 'running')
        return Promise.reject(Error('Game audio is unavailable. Close this message, then choose Play to try again.'));
      if (typeof context.getOutputTimestamp !== 'function')
        return Promise.reject(Error('Game audio output timing is unavailable. Close this message, then choose Play to try again.'));
      if (renderWaiters.size) return Promise.reject(Error('Audio readiness is already pending.'));
      const id = ++renderSequence;
      return new Promise((resolve, reject) => {
        const waiter = {id, resolve, reject, timer: null, pollTimer: null, processTime: null};
        waiter.timer = setTimeout(() => {
          renderWaiters.delete(id); clearRenderWaiter(waiter);
          reject(Error('Game audio did not start. Close this message, then choose Play to try again.'));
        }, 5000);
        renderWaiters.set(id, waiter);
        try { node.port.postMessage({type: 'render-ready-request', id}); }
        catch (error) { renderWaiters.delete(id); clearRenderWaiter(waiter); reject(error); }
      });
    },
    setEnabled,
    async pause() { setEnabled(false); await waitForAck(); },
    waitForAck,
    readyForPreparation: () => !node || (!enabled && acknowledged),
    write(pcm) { if (enabled) node?.port.postMessage({type: 'pcm', pcm}, [pcm.buffer]); },
    fail(error) {
      clearRenderWaiters(error);
      for (const waiter of waiters.splice(0)) { clearTimeout(waiter.timer); waiter.reject(error); }
    },
    async destroy() { clearRenderWaiters(Error('Audio output closed.')); node?.disconnect(); if (context) await context.close(); },
  });
}
