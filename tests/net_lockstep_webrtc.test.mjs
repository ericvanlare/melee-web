import assert from 'node:assert/strict';
import test from 'node:test';
import {LockstepPeer} from '../scripts/net_lockstep_core.mjs';
import {
  createDataChannelEndpoint, WEBRTC_MAX_BUFFERED_BYTES,
  WEBRTC_MAX_INBOUND_PENDING_MESSAGES,
} from '../scripts/net_lockstep_webrtc.mjs';

function event(type, fields = {}) {
  return {type, ...fields};
}

class FakeDataChannel {
  constructor() {
    this.listeners = new Map();
    this.ordered = true;
    this.maxRetransmits = null;
    this.maxPacketLifeTime = null;
    this.readyState = 'connecting';
    this.bufferedAmount = 0;
    this.bufferedAmountLowThreshold = 0;
    this.sent = [];
    this.partner = null;
    this.throwOnSend = false;
    this.autoDrain = true;
  }
  addEventListener(type, listener, {once = false} = {}) {
    const rows = this.listeners.get(type) ?? [];
    rows.push({listener, once});
    this.listeners.set(type, rows);
  }
  removeEventListener(type, listener) {
    const rows = this.listeners.get(type) ?? [];
    const remaining = rows.filter(row => row.listener !== listener);
    if (remaining.length) this.listeners.set(type, remaining);
    else this.listeners.delete(type);
  }
  dispatchEvent(value) {
    for (const row of [...(this.listeners.get(value.type) ?? [])]) {
      row.listener(value);
      if (row.once) this.removeEventListener(value.type, row.listener);
    }
    return true;
  }
  listenerCount(type) { return (this.listeners.get(type) ?? []).length; }
  open() {
    if (this.readyState !== 'connecting') throw Error('fake channel is not connecting');
    this.readyState = 'open';
    this.dispatchEvent(event('open'));
  }
  deliver(data) { this.dispatchEvent(event('message', {data})); }
  send(text) {
    if (this.throwOnSend) throw Error('controlled send throw');
    if (this.readyState !== 'open') throw Error('fake channel is not open');
    this.sent.push(text);
    this.bufferedAmount += new TextEncoder().encode(text).byteLength;
    if (this.partner?.readyState === 'open') this.partner.deliver(text);
    if (this.autoDrain) queueMicrotask(() => this.drain());
  }
  drain() {
    const previous = this.bufferedAmount;
    this.bufferedAmount = 0;
    if (previous > this.bufferedAmountLowThreshold)
      this.dispatchEvent(event('bufferedamountlow'));
  }
  finishClose() {
    if (this.readyState === 'closed') return;
    this.readyState = 'closed';
    this.dispatchEvent(event('close'));
  }
  close() {
    if (this.readyState === 'closed' || this.readyState === 'closing') return;
    this.readyState = 'closing';
    queueMicrotask(() => {
      this.finishClose();
      this.partner?.finishClose();
    });
  }
}

function pairedChannels() {
  const alpha = new FakeDataChannel(), beta = new FakeDataChannel();
  alpha.partner = beta;
  beta.partner = alpha;
  return {alpha, beta};
}

function makeEndpoint(channel, role, extra = {}) {
  return createDataChannelEndpoint({channel, role, onMessage: () => {}, ...extra});
}

test('portable A2 identity hello crosses the endpoint contract with no native API calls', async () => {
  const channels = pairedChannels();
  const received = {alpha: [], beta: []};
  const sinkFrames = {alpha: [], beta: []};
  const disconnects = {alpha: 0, beta: 0};
  let nativeApiCalls = 0;
  const peers = {};
  for (const role of ['alpha', 'beta']) {
    peers[role] = new LockstepPeer({role, sourceTicks: 2, inputTicks: 0,
      pushFrame: async (firstTick, bytes) => sinkFrames[role].push({firstTick, bytes: bytes.length}),
    });
  }
  const endpoints = {};
  for (const role of ['alpha', 'beta']) {
    const channel = channels[role];
    endpoints[role] = createDataChannelEndpoint({channel, role, timeoutMs: 500,
      onMessage: text => { received[role].push(text); return peers[role].receive(text); },
      onDisconnect: () => { ++disconnects[role]; },
    });
    peers[role].attach(text => endpoints[role].send(text));
  }
  channels.alpha.open();
  channels.beta.open();
  await Promise.all([endpoints.alpha.ready, endpoints.beta.ready]);
  const agreement = {build: 'synthetic-browser-webrtc-boundary', start: 'shared-css'};
  await Promise.all([peers.alpha.start(agreement), peers.beta.start(agreement)]);
  await Promise.all([endpoints.alpha.drainInbound(), endpoints.beta.drainInbound()]);

  assert(peers.alpha.ready && peers.beta.ready);
  assert.equal(peers.alpha.agreementHash, peers.beta.agreementHash);
  assert.deepEqual(
    [peers.alpha.localPort, peers.alpha.remotePort, peers.beta.localPort, peers.beta.remotePort],
    [0, 1, 1, 0],
  );
  assert.deepEqual(received.alpha.map(row => JSON.parse(row).type), ['hello']);
  assert.deepEqual(received.beta.map(row => JSON.parse(row).type), ['hello']);
  assert.equal(JSON.parse(received.alpha[0]).role, 'beta');
  assert.equal(JSON.parse(received.beta[0]).role, 'alpha');
  assert.deepEqual(sinkFrames, {
    alpha: [{firstTick: 0, bytes: 2 * 44}],
    beta: [{firstTick: 0, bytes: 2 * 44}],
  }, 'the portable core may send its neutral bootstrap batch only to the test sink');
  assert.equal(nativeApiCalls, 0, 'this milestone makes no native runtime calls');
  assert.equal(endpoints.alpha.transport.type, 'webrtc-datachannel');
  assert.equal(endpoints.alpha.transport.ordered, true);
  assert.equal(endpoints.alpha.transport.reliable, true);

  await Promise.all([endpoints.alpha.close(), endpoints.beta.close()]);
  assert(endpoints.alpha.closed && endpoints.beta.closed);
  assert.deepEqual(disconnects, {alpha: 1, beta: 1});
});

test('the first receiver is installed before open and handles a packet at the open boundary', async () => {
  const channel = new FakeDataChannel();
  const received = [];
  const endpoint = createDataChannelEndpoint({channel, role: 'alpha',
    onMessage: text => received.push(text)});
  assert.equal(channel.listenerCount('message'), 1);
  channel.open();
  channel.deliver('first packet');
  await endpoint.ready;
  await endpoint.drainInbound();
  assert.deepEqual(received, ['first packet']);
  await endpoint.close();
});

test('close before open rejects readiness and still delivers one disconnect callback', async () => {
  const channel = new FakeDataChannel();
  const disconnects = [];
  const endpoint = endpointForFailure(channel, 'alpha', {
    onDisconnect: (_role, reason) => disconnects.push(reason), timeoutMs: 100,
  });
  channel.finishClose();
  await assert.rejects(endpoint.ready, /closed before opening/);
  await endpoint.close();
  assert.equal(disconnects.length, 1);
  assert(endpoint.closed);
});

test('unreliable or unordered channels are rejected before listeners are installed', () => {
  const unordered = new FakeDataChannel();
  unordered.ordered = false;
  assert.throws(() => makeEndpoint(unordered, 'alpha'), /reliable and ordered/);
  const lossy = new FakeDataChannel();
  lossy.maxRetransmits = 0;
  assert.throws(() => makeEndpoint(lossy, 'alpha'), /reliable and ordered/);
  assert.equal(unordered.listenerCount('message'), 0);
});

test('flush waits for the local send buffer and does not imply peer acknowledgement', async () => {
  const channel = new FakeDataChannel();
  channel.autoDrain = false;
  const endpoint = makeEndpoint(channel, 'alpha');
  channel.open();
  await endpoint.ready;
  await endpoint.send('buffered');
  let flushed = false;
  const flush = endpoint.flush(100).then(() => { flushed = true; });
  await Promise.resolve();
  assert.equal(flushed, false);
  channel.drain();
  await flush;
  assert.equal(flushed, true);
  await endpoint.close();
});

test('channel error and synchronous send throw fail closed and notify once', async () => {
  const channel = new FakeDataChannel();
  const reports = [], disconnects = [];
  const endpoint = endpointForFailure(channel, 'alpha', {
    onEndpointError: (_role, error) => reports.push(error.message),
    onDisconnect: () => disconnects.push(true),
  });
  channel.open();
  await endpoint.ready;
  channel.dispatchEvent(event('error', {error: Error('controlled channel error')}));
  await waitForClosed(channel);
  assert.equal(reports.length, 1);
  await assert.rejects(endpoint.drainInbound(), /inbound callback failed/);
  await endpoint.close();
  assert(endpoint.closed);
  assert.equal(disconnects.length, 1);

  const throwing = new FakeDataChannel();
  const sendReports = [];
  const sendEndpoint = endpointForFailure(throwing, 'beta', {
    onEndpointError: (_role, error) => sendReports.push(error.message),
  });
  throwing.open();
  await sendEndpoint.ready;
  throwing.throwOnSend = true;
  await assert.rejects(sendEndpoint.send('fails'), /controlled send throw/);
  await waitForClosed(throwing);
  assert.deepEqual(sendReports, ['controlled send throw']);
  await assert.rejects(sendEndpoint.close(), /did not close cleanly/);
});

test('outbound and inbound callback queue overflow are bounded and terminal', async () => {
  const outboundChannel = new FakeDataChannel();
  const outbound = makeEndpoint(outboundChannel, 'alpha');
  outboundChannel.open();
  await outbound.ready;
  outboundChannel.bufferedAmount = WEBRTC_MAX_BUFFERED_BYTES - 1;
  await assert.rejects(outbound.send('too large for remaining buffer'), /queued send bytes would exceed 1 MiB/);
  await waitForClosed(outboundChannel);
  await assert.rejects(outbound.close(), /did not close cleanly/);

  const countChannel = new FakeDataChannel();
  const countBounded = createDataChannelEndpoint({channel: countChannel, role: 'alpha', timeoutMs: 500,
    onMessage: () => {},
  });
  const queuedSends = Array.from({length: 256}, () => countBounded.send('x'));
  await assert.rejects(countBounded.send('overflow'), /queued send bytes would exceed 1 MiB/);
  await waitForClosed(countChannel);
  const sendResults = await Promise.allSettled(queuedSends);
  assert.equal(sendResults.length, 256);
  assert(sendResults.every(result => result.status === 'rejected'));
  await assert.rejects(countBounded.close(), /did not close cleanly/);

  const inboundChannel = new FakeDataChannel();
  let release;
  const gate = new Promise(resolve => { release = resolve; });
  let callbacks = 0;
  const inbound = createDataChannelEndpoint({channel: inboundChannel, role: 'beta', timeoutMs: 500,
    onMessage: async () => { ++callbacks; await gate; },
  });
  inboundChannel.open();
  await inbound.ready;
  for (let index = 0; index < WEBRTC_MAX_INBOUND_PENDING_MESSAGES + 1; ++index)
    inboundChannel.deliver('packet');
  release();
  await waitForClosed(inboundChannel);
  await assert.rejects(inbound.drainInbound(), /inbound callback failed/);
  assert.equal(callbacks, WEBRTC_MAX_INBOUND_PENDING_MESSAGES);
  await inbound.close();
});

test('repeated close reuses the same promise and retains disconnect cleanup errors', async () => {
  const channel = new FakeDataChannel();
  let disconnectCalls = 0;
  const endpoint = endpointForFailure(channel, 'beta', {
    onDisconnect: () => { ++disconnectCalls; throw Error('disconnect cleanup sentinel'); },
  });
  channel.open();
  await endpoint.ready;
  const first = endpoint.close();
  const second = endpoint.close();
  assert.equal(first, second);
  await assert.rejects(first, error => {
    assert(error instanceof AggregateError);
    assert.match(errorMessages(error).join('\n'), /disconnect cleanup sentinel/);
    return true;
  });
  assert.equal(disconnectCalls, 1);
});

function endpointForFailure(channel, role, options = {}) {
  return createDataChannelEndpoint({channel, role, onMessage: () => {}, ...options});
}

function errorMessages(error) {
  return [String(error?.message || error),
    ...(Array.isArray(error?.errors) ? error.errors.flatMap(errorMessages) : [])];
}

async function waitForClosed(channel) {
  if (channel.readyState === 'closed') return;
  await new Promise((resolve, reject) => {
    const timeout = setTimeout(() => {
      channel.removeEventListener('close', closed);
      reject(Error('controlled data channel did not close'));
    }, 100);
    const closed = () => { clearTimeout(timeout); resolve(); };
    channel.addEventListener('close', closed, {once: true});
  });
}
