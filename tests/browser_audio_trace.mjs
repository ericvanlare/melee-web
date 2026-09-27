/** Install a read-only Web Audio/PCM observer before the page module graph. */
export function installBrowserAudioTrace() {
  const trace = {contexts: [], worklets: [], nodes: new WeakMap(), ports: new WeakMap(), installErrors: []};
  const rememberError = error => trace.installErrors.push(String(error?.message || error));
  const nonzeroPcm = (record, pcm) => {
    record.pcmMessages++;
    if (!ArrayBuffer.isView(pcm) || pcm.length === 0) return;
    record.pcmFrames += Math.floor(pcm.length / 2);
    let nonzero = 0;
    for (const value of pcm) {
      if (Number.isFinite(value) && Math.abs(value) > 1e-8) nonzero++;
    }
    if (nonzero) {
      record.nonzeroPcmMessages++;
      record.nonzeroPcmSamples += nonzero;
    }
  };
  const snapshot = () => ({
    contexts: trace.contexts.map(record => ({
      sampleRate: record.sampleRate,
      state: record.context.state,
      states: [...record.states],
      resumes: record.resumes,
      closed: record.closed,
    })),
    worklets: trace.worklets.map(record => ({
      name: record.name,
      sampleRate: record.sampleRate,
      connected: record.connected,
      destinationConnected: record.destinationConnected,
      pcmMessages: record.pcmMessages,
      nonzeroPcmMessages: record.nonzeroPcmMessages,
      nonzeroPcmSamples: record.nonzeroPcmSamples,
      pcmFrames: record.pcmFrames,
    })),
    installErrors: [...trace.installErrors],
  });
  window.audioPreviewTrace = {snapshot};

  const NativeAudioContext = window.AudioContext;
  if (NativeAudioContext) {
    const AudioContextProxy = function(...args) {
      const audioContext = new NativeAudioContext(...args);
      const record = {context: audioContext, sampleRate: audioContext.sampleRate,
        states: [audioContext.state], resumes: 0, closed: false};
      trace.contexts.push(record);
      for (const methodName of ['resume', 'suspend', 'close']) {
        const method = audioContext[methodName];
        if (typeof method !== 'function') continue;
        try {
          audioContext[methodName] = async (...methodArgs) => {
            const result = await method.apply(audioContext, methodArgs);
            record.states.push(audioContext.state);
            if (methodName === 'resume') record.resumes++;
            if (methodName === 'close') record.closed = true;
            return result;
          };
        } catch (error) { rememberError(error); }
      }
      return audioContext;
    };
    AudioContextProxy.prototype = NativeAudioContext.prototype;
    try {
      Object.defineProperty(window, 'AudioContext', {value: AudioContextProxy,
        configurable: true, writable: true});
    } catch (error) { rememberError(error); }
  }

  const NativeAudioNode = window.AudioNode;
  const nativeConnect = NativeAudioNode?.prototype?.connect;
  if (nativeConnect) {
    try {
      Object.defineProperty(NativeAudioNode.prototype, 'connect', {
        configurable: true,
        writable: true,
        value(destination, ...args) {
          const record = trace.nodes.get(this);
          if (record) {
            record.connected = true;
            record.destinationConnected ||= destination === record.context.destination;
          }
          return nativeConnect.call(this, destination, ...args);
        },
      });
    } catch (error) { rememberError(error); }
  }

  const NativeMessagePort = window.MessagePort;
  const nativePostMessage = NativeMessagePort?.prototype?.postMessage;
  if (nativePostMessage) {
    try {
      Object.defineProperty(NativeMessagePort.prototype, 'postMessage', {
        configurable: true,
        writable: true,
        value(data, ...args) {
          const record = trace.ports.get(this);
          if (record && data?.type === 'pcm') nonzeroPcm(record, data.pcm);
          return nativePostMessage.call(this, data, ...args);
        },
      });
    } catch (error) { rememberError(error); }
  }

  const NativeAudioWorkletNode = window.AudioWorkletNode;
  if (NativeAudioWorkletNode) {
    const AudioWorkletNodeProxy = function(audioContext, name, options) {
      const node = new NativeAudioWorkletNode(audioContext, name, options);
      const record = {context: audioContext, name, sampleRate: audioContext.sampleRate,
        connected: false, destinationConnected: false, pcmMessages: 0,
        nonzeroPcmMessages: 0, nonzeroPcmSamples: 0, pcmFrames: 0};
      trace.worklets.push(record);
      trace.nodes.set(node, record);
      trace.ports.set(node.port, record);
      return node;
    };
    AudioWorkletNodeProxy.prototype = NativeAudioWorkletNode.prototype;
    try {
      Object.defineProperty(window, 'AudioWorkletNode', {value: AudioWorkletNodeProxy,
        configurable: true, writable: true});
    } catch (error) { rememberError(error); }
  }
}
