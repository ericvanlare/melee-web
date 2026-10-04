import assert from 'node:assert/strict';

import {createRuntimeAudio} from '../web/runtime-audio.mjs';

class FakeTimers {
  constructor() { this.now = 0; this.nextId = 1; this.pending = new Map(); }

  setTimeout(callback, delay = 0) {
    const id = this.nextId++;
    this.pending.set(id, {at: this.now + Math.max(0, Number(delay) || 0), callback});
    return id;
  }

  clearTimeout(id) { this.pending.delete(id); }

  runAll() {
    while (this.pending.size) {
      const [id, timer] = [...this.pending.entries()]
        .sort((left, right) => left[1].at - right[1].at)[0];
      this.pending.delete(id);
      this.now = timer.at;
      timer.callback();
    }
  }
}

class FakePort {
  constructor() {
    this.messages = [];
    this.onmessage = null;
    this.failNextRender = false;
  }

  postMessage(data, transfer = []) {
    this.messages.push({data, transfer});
    if (data?.type === 'render-ready-request' && this.failNextRender) {
      this.failNextRender = false;
      throw Error('synthetic postMessage failure');
    }
    if (data?.type === 'state') {
      this.emit({type: 'state-ack', enabled: data.enabled});
    }
  }

  emit(data) { this.onmessage?.({data}); }
}

class FakeAudioContext {
  static instances = [];

  constructor() {
    this.state = 'running';
    this.currentTime = 0;
    this.sampleRate = 32000;
    this.audioWorklet = {addModule: async () => {}};
    this.destination = {};
    this.closed = false;
    FakeAudioContext.instances.push(this);
  }

  resume() { return Promise.resolve(); }

  close() {
    this.closed = true;
    this.state = 'closed';
    return Promise.resolve();
  }
}

class FakeAudioWorkletNode {
  static instances = [];

  constructor() {
    this.port = new FakePort();
    this.disconnected = false;
    FakeAudioWorkletNode.instances.push(this);
  }

  connect() {}
  disconnect() { this.disconnected = true; }
}

const flush = async () => {
  await Promise.resolve();
  await Promise.resolve();
};

async function withAudio(test, hooks = {}) {
  const original = {
    AudioContext: globalThis.AudioContext,
    AudioWorkletNode: globalThis.AudioWorkletNode,
    setTimeout: globalThis.setTimeout,
    clearTimeout: globalThis.clearTimeout,
  };
  const timers = new FakeTimers();
  FakeAudioContext.instances.length = 0;
  FakeAudioWorkletNode.instances.length = 0;
  globalThis.AudioContext = FakeAudioContext;
  globalThis.AudioWorkletNode = FakeAudioWorkletNode;
  globalThis.setTimeout = timers.setTimeout.bind(timers);
  globalThis.clearTimeout = timers.clearTimeout.bind(timers);
  try {
    const events = [];
    let audio;
    audio = createRuntimeAudio({
      assetBase: 'https://example.test/runtime/',
      onEvent: event => events.push(event),
      onError: error => { events.push({error: error.message}); hooks.onError?.(error, audio); },
      onFatal: error => { events.push({fatal: error.message}); hooks.onFatal?.(error, audio); },
    });
    await audio.prepare();
    await flush();
    const result = await test({audio, node: FakeAudioWorkletNode.instances.at(-1), context: FakeAudioContext.instances.at(-1), events, timers});
    assert.equal(timers.pending.size, 0, 'Settled protocol operations leave no timers behind');
    return result;
  } finally {
    if (original.AudioContext === undefined) delete globalThis.AudioContext;
    else globalThis.AudioContext = original.AudioContext;
    if (original.AudioWorkletNode === undefined) delete globalThis.AudioWorkletNode;
    else globalThis.AudioWorkletNode = original.AudioWorkletNode;
    globalThis.setTimeout = original.setTimeout;
    globalThis.clearTimeout = original.clearTimeout;
  }
}

// A state acknowledgement is not a render-process acknowledgement.
await withAudio(async ({audio, node}) => {
  const pending = audio.waitForRender();
  let settled = false;
  pending.then(() => { settled = true; }, () => { settled = true; });
  await flush();
  assert.equal(settled, false, 'render wait must remain pending until process acknowledgement');
  const request = node.port.messages.at(-1).data;
  assert.equal(request.type, 'render-ready-request');
  node.port.emit({type: 'state-ack', enabled: false});
  await flush();
  assert.equal(settled, false, 'state-ack must not resolve render wait');
  node.port.emit({type: 'render-ready', id: request.id, process_time: 1.25, process_frame: 2048});
  await pending;
});

// Only the matching nonce resolves; unrelated and late acknowledgements do not.
await withAudio(async ({audio, node, timers}) => {
  const first = audio.waitForRender();
  const firstId = node.port.messages.at(-1).data.id;
  node.port.emit({type: 'render-ready', id: firstId + 1, process_time: 1, process_frame: 1});
  let firstSettled = false;
  first.then(() => { firstSettled = true; }, () => { firstSettled = true; });
  await flush();
  assert.equal(firstSettled, false, 'wrong render nonce must be ignored');
  node.port.emit({type: 'render-ready', id: firstId, process_time: 1.5, process_frame: 512});
  await first;

  const timedOut = audio.waitForRender();
  const timedOutId = node.port.messages.at(-1).data.id;
  const beforeTimeout = timers.now;
  timers.runAll();
  assert.equal(timers.now - beforeTimeout, 5000, 'Render readiness has a five-second deadline');
  await assert.rejects(timedOut, /Game audio did not start/);

  const retry = audio.waitForRender();
  const retryId = node.port.messages.at(-1).data.id;
  assert.notEqual(retryId, timedOutId, 'retry must use a fresh nonce');
  node.port.emit({type: 'render-ready', id: timedOutId, process_time: 2, process_frame: 1024});
  let retrySettled = false;
  retry.then(() => { retrySettled = true; }, () => { retrySettled = true; });
  await flush();
  assert.equal(retrySettled, false, 'late timeout acknowledgement must not resolve a retry');
  node.port.emit({type: 'render-ready', id: retryId, process_time: 2.5, process_frame: 1536});
  await retry;
});

// Pending waits are rejected by explicit failure and destroy cleanup.
await withAudio(async ({audio, node, context}) => {
  const failed = audio.waitForRender();
  audio.fail(Error('synthetic failure'));
  await assert.rejects(failed, /synthetic failure/);

  const closed = audio.waitForRender();
  const destroyed = audio.destroy();
  await assert.rejects(closed, /closed|destroy/i);
  await destroyed;
  assert.equal(node.disconnected, true, 'destroy must disconnect the worklet node');
  assert.equal(context.closed, true, 'destroy must close the audio context');
});

// A worklet transport error is terminal. The owner's fatal callback rejects
// any pending render wait through the transport fail hook, preserving the
// original worklet error for the sanitized runtime_failure path.
await withAudio(async ({audio, node, events}) => {
  const pending = audio.waitForRender();
  node.port.emit({type: 'audio-error', error: 'Audio output queue overflow'});
  assert.deepEqual(events.filter(event => event.fatal), [
    {fatal: 'Audio output queue overflow'},
  ], 'worklet errors must route only through the fatal callback');
  assert.equal(events.some(event => event.error === 'Audio output queue overflow' && !event.type), false,
    'worklet errors must not route through recoverable onError');
  await assert.rejects(pending, /Audio output queue overflow/);
}, {onFatal: (error, audio) => audio.fail(error)});

await withAudio(async ({audio, node, events}) => {
  const pending = audio.waitForRender();
  node.onprocessorerror();
  assert.deepEqual(events.filter(event => event.fatal), [
    {fatal: 'Game audio stopped unexpectedly. Reload to recover.'},
  ], 'processor exceptions must use the terminal owner path even without a port message');
  await assert.rejects(pending, /Game audio stopped unexpectedly/);
}, {onFatal: (error, audio) => audio.fail(error)});

// Unavailable and suspended contexts reject without creating an unbounded wait.
{
  const audio = createRuntimeAudio({assetBase: 'https://example.test/runtime/'});
  await assert.rejects(audio.waitForRender(), /unavailable|before launch/i);
}
await withAudio(async ({audio, context}) => {
  context.state = 'suspended';
  await assert.rejects(audio.waitForRender(), /unavailable|before launch/i);
});

// Duplicate pending requests are rejected, while a synchronous post failure
// clears the failed request so the next request can proceed.
await withAudio(async ({audio, node}) => {
  const pending = audio.waitForRender();
  await assert.rejects(audio.waitForRender(), /already pending|pending/i);
  const firstId = node.port.messages.at(-1).data.id;
  node.port.emit({type: 'render-ready', id: firstId, process_time: 3, process_frame: 2048});
  await pending;

  node.port.failNextRender = true;
  await assert.rejects(audio.waitForRender(), /postMessage failure/);
  const retry = audio.waitForRender();
  const retryId = node.port.messages.at(-1).data.id;
  node.port.emit({type: 'render-ready', id: retryId, process_time: 3.5, process_frame: 2560});
  await retry;
});

console.log('Runtime audio render readiness protocol, timeout, failure and cleanup passed');
