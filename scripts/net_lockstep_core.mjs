export const LOCKSTEP_VERSION = 1;
export const LOCKSTEP_DELAY = 2;
export const LOCKSTEP_WINDOW = 32;
export const LOCKSTEP_MAX_BATCH = 32;
export const LOCKSTEP_MAX_PENDING_RECEIVE_MESSAGES = 64;
export const LOCKSTEP_MAX_PENDING_RECEIVE_BYTES = 1024 * 1024;
export const NET_RECORD_BYTES = 64;
export const PAD_BYTES = 11;
export const NET_FRAME_BYTES = 44;
export const TERMINAL = Object.freeze({desync: 1, disconnect: 2, protocol: 3, startIdentity: 4});

const opposite = Object.freeze({alpha: 'beta', beta: 'alpha'});
const portFor = Object.freeze({alpha: 0, beta: 1});
const zeroPad = new Uint8Array(PAD_BYTES);
const noControllerPad = new Uint8Array([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff]);
const clone = value => JSON.parse(JSON.stringify(value));
const utf8 = new TextEncoder();

function digestHex(bytes) {
  if (!globalThis.crypto?.subtle?.digest)
    return Promise.reject(Error('WebCrypto SHA-256 is unavailable'));
  return globalThis.crypto.subtle.digest('SHA-256', bytes).then(result => toHex(new Uint8Array(result)));
}

function digestText(text) { return digestHex(utf8.encode(text)); }

function toHex(bytes) {
  let result = '';
  for (const byte of bytes) result += byte.toString(16).padStart(2, '0');
  return result;
}

function bytesFrom(value, label) {
  // Some Node byte views override slice() to return shared storage; always
  // allocate an independent copy at the protocol ownership boundary.
  if (value instanceof Uint8Array) return new Uint8Array(value);
  if (ArrayBuffer.isView(value) && value.BYTES_PER_ELEMENT === 1)
    return new Uint8Array(value.buffer, value.byteOffset, value.byteLength).slice();
  if (value instanceof ArrayBuffer) return new Uint8Array(value.slice(0));
  if (Array.isArray(value)) return Uint8Array.from(value);
  throw Error(`${label} must be a byte array`);
}

function equalBytes(left, right) {
  if (left.length !== right.length) return false;
  for (let index = 0; index < left.length; ++index)
    if (left[index] !== right[index]) return false;
  return true;
}

function viewOf(bytes) { return new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength); }

function encodeBase64(bytes) {
  let binary = '';
  for (let offset = 0; offset < bytes.length; offset += 0x8000)
    binary += String.fromCharCode(...bytes.subarray(offset, offset + 0x8000));
  return btoa(binary);
}

function decodeBase64(text) {
  const binary = atob(text);
  const bytes = new Uint8Array(binary.length);
  for (let index = 0; index < binary.length; ++index) bytes[index] = binary.charCodeAt(index);
  return bytes;
}

function integer(value, max, label) {
  if (!Number.isSafeInteger(value) || value < 0 || value > max)
    throw Error(`Invalid lockstep ${label}`);
  return value;
}

function decodeSample(text) {
  if (typeof text !== 'string' || !/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(text))
    throw Error('Invalid lockstep PAD sample encoding');
  let value;
  try { value = decodeBase64(text); }
  catch { throw Error('Invalid lockstep PAD sample encoding'); }
  if (value.length !== PAD_BYTES || encodeBase64(value) !== text)
    throw Error('Lockstep PAD sample must contain exactly 11 canonical bytes');
  return value;
}

function recordTick(record) {
  if (!(record instanceof Uint8Array) || record.length !== NET_RECORD_BYTES)
    throw Error('Lockstep checksum must be one 64-byte native record');
  return viewOf(record).getUint32(0, true);
}

function mismatchChannel(a, b) {
  const fields = [
    [0, 16, 0], // header
    [24, 8, 1], // four PAD records
    [32, 8, 2], // HSD master PAD
    [16, 4, 3], // checksum component flags
    [40, 8, 3], // scene state and flags
    [56, 8, 4], // total
    [20, 4, 5], // object count
    [48, 8, 5], // object state
  ];
  for (const [offset, size, channel] of fields)
    if (!equalBytes(a.subarray(offset, offset + size), b.subarray(offset, offset + size))) return channel;
  return 0;
}

/** One side of the bounded A2 peer protocol. The transport is an opaque
 * send(string) callback; it never assembles PAD frames or edits checksums. */
export class LockstepPeer {
  constructor({role, sourceTicks, inputTicks, pushFrame, onTerminal = () => {}, onReady = () => {}}) {
    if (!(role in opposite)) throw Error('Lockstep role must be alpha or beta');
    this.role = role;
    this.remoteRole = opposite[role];
    this.localPort = portFor[role];
    this.remotePort = portFor[this.remoteRole];
    this.sourceTicks = integer(sourceTicks, 216000, 'source tick bound');
    this.inputTicks = integer(inputTicks, 216000, 'input tick bound');
    if (this.sourceTicks < LOCKSTEP_DELAY || this.inputTicks + LOCKSTEP_DELAY !== this.sourceTicks)
      throw Error('Lockstep input and source tick bounds must differ by the fixed two-tick delay');
    this.pushFrame = pushFrame;
    this.onTerminal = onTerminal;
    this.onReady = onReady;
    this.sendRaw = null;
    this.localHello = null;
    this.startAttempted = false;
    this.startPreparation = null;
    this.remoteHello = null;
    this.ready = false;
    this.terminal = null;
    this.local = new Map();
    this.nativeLocal = new Map();
    this.deferredInputs = new Set();
    this.remote = new Map();
    this.localChecksums = new Map();
    this.remoteChecksums = new Map();
    this.remoteAckInput = -1;
    this.remoteAckChecksum = -1;
    this.remoteContiguousInput = -1;
    this.localContiguousInput = -1;
    this.remoteContiguousChecksum = -1;
    this.localContiguousChecksum = -1;
    this.nextChecksumCompare = 0;
    this.nextSequence = 0;
    this.expectedSequence = 0;
    this.lastAccepted = new Map();
    this.pendingSequence = new Map();
    this.nextSourceFrame = 0;
    this.delayedChecksumThrough = -1;
    this.inputDuplicates = 0;
    this.checksumDuplicates = 0;
    this.outOfOrderInputs = 0;
    this.acknowledgedInputs = 0;
    this.acknowledgedChecksums = 0;
    this.checksumMismatches = [];
    this.pump = Promise.resolve();
    this.receiveQueue = Promise.resolve();
    this.receiveQueueCount = 0;
    this.receiveQueueBytes = 0;
    this.receiveOverflowError = null;
    this.receiveOverflowPromise = null;
  }

  attach(sendRaw) { this.sendRaw = sendRaw; }

  async sendObject(object) {
    if (!this.sendRaw || this.terminal) return;
    await this.sendRaw(JSON.stringify(object));
  }

  async start(agreement) {
    if (this.startAttempted) throw Error('Lockstep start identity was already sent');
    this.startAttempted = true;
    const hello = {
      type: 'hello', version: LOCKSTEP_VERSION, role: this.role,
      local_port: this.localPort, remote_port: this.remotePort,
      agreement: clone(agreement),
    };
    this.startPreparation = (async () => {
      let hash;
      try { hash = await digestText(JSON.stringify(hello.agreement)); }
      catch (cause) {
        const reason = `start identity SHA-256 failed: ${String(cause?.message || cause)}`;
        try { await this.fail('protocol', {reason}); }
        catch (error) { throw Error(reason, {cause: error}); }
        throw Error(reason, {cause});
      }
      // Publish both identity fields in one synchronous turn before the hello
      // can leave this peer or a waiting remote hello can make us ready.
      this.localHello = hello;
      this.agreementHash = hash;
    })();
    await this.startPreparation;
    await this.sendObject(this.localHello);
  }

  async receive(text) {
    if (this.terminal) return;
    if (typeof text !== 'string')
      return this.#receiveOverflow('Lockstep inbound messages must be UTF-8 text');
    if (text.length > LOCKSTEP_MAX_PENDING_RECEIVE_BYTES)
      return this.#receiveOverflow('Lockstep inbound receive queue exceeded its 1 MiB byte bound');
    const byteLength = utf8.encode(text).byteLength;
    if (byteLength > LOCKSTEP_MAX_PENDING_RECEIVE_BYTES)
      return this.#receiveOverflow('Lockstep inbound receive queue exceeded its 1 MiB byte bound');
    if (this.receiveOverflowError)
      return this.receiveOverflowPromise;
    if (this.receiveQueueCount >= LOCKSTEP_MAX_PENDING_RECEIVE_MESSAGES ||
        byteLength > LOCKSTEP_MAX_PENDING_RECEIVE_BYTES - this.receiveQueueBytes)
      return this.#receiveOverflow('Lockstep inbound receive queue exceeded its bounded capacity');
    ++this.receiveQueueCount;
    this.receiveQueueBytes += byteLength;
    const result = this.receiveQueue.then(() => this.#receiveOne(text));
    const tracked = result.finally(() => {
      --this.receiveQueueCount;
      this.receiveQueueBytes -= byteLength;
    });
    this.receiveQueue = tracked.catch(() => {});
    return tracked;
  }

  #receiveOverflow(reason) {
    if (!this.receiveOverflowError) {
      this.receiveOverflowError = Error(reason);
      const terminal = this.fail('protocol', {reason});
      this.receiveOverflowPromise = Promise.resolve(terminal).then(() => {
        throw this.receiveOverflowError;
      });
      this.receiveOverflowPromise.catch(() => {});
    }
    return this.receiveOverflowPromise;
  }

  async #receiveOne(text) {
    if (this.terminal) return;
    if (this.receiveOverflowError) throw this.receiveOverflowError;
    let packet;
    try { packet = JSON.parse(text); }
    catch (error) {
      await this.fail('protocol', {reason: 'invalid JSON'});
      throw error;
    }
    try {
      if (packet?.type === 'hello') return await this.#receiveHello(packet);
      if (packet?.type === 'terminal' && packet.version === LOCKSTEP_VERSION &&
          packet.role === this.remoteRole &&
          ['desync', 'disconnect', 'protocol', 'startIdentity'].includes(packet.kind))
        return await this.fail(packet.kind, packet.details ?? {}, {notify: false});
      if (!this.ready) throw Error('state arrived before peer identity agreement');
      if (packet?.type !== 'state' || packet.version !== LOCKSTEP_VERSION ||
          packet.role !== this.remoteRole) throw Error('invalid state packet identity');
      await this.#receiveState(packet, text);
    } catch (error) {
      await this.fail('protocol', {reason: String(error.message || error)});
      throw error;
    }
  }

  async #receiveHello(packet) {
    if (this.startPreparation) await this.startPreparation;
    if (this.terminal) return;
    if (!this.localHello || packet.version !== LOCKSTEP_VERSION ||
        packet.role !== this.remoteRole || packet.local_port !== this.remotePort ||
        packet.remote_port !== this.localPort || !packet.agreement ||
        JSON.stringify(packet.agreement) !== JSON.stringify(this.localHello.agreement))
      throw Error('peer start identity or complementary PAD-port ownership differs');
    if (this.remoteHello && JSON.stringify(this.remoteHello) !== JSON.stringify(packet))
      throw Error('conflicting peer start identity retransmission');
    this.remoteHello = clone(packet);
    if (!this.ready) {
      this.ready = true;
      await this.onReady(this);
      await this.#pumpFrames();
    }
  }

  async addLocalInput(tick, sample, {repeat = false, deferSend = false, nativeSample = sample} = {}) {
    return this.addLocalInputs([[tick, sample]], {repeat, deferSend,
      nativeOverrides: nativeSample === sample ? [] : [[tick, nativeSample]]});
  }

  async addLocalInputs(entries, {repeat = false, deferSend = false, nativeOverrides = []} = {}) {
    this.#assertReady();
    if (!Array.isArray(entries) || !entries.length || entries.length > LOCKSTEP_MAX_BATCH)
      throw Error(`Lockstep local input batches must contain 1..${LOCKSTEP_MAX_BATCH} contributions`);
    const seen = new Set();
    const overrides = new Map(nativeOverrides);
    for (const [tick, sample] of entries) {
      integer(tick, this.inputTicks - 1, 'input tick');
      if (seen.has(tick)) throw Error('Local input batch repeats an input tick');
      seen.add(tick);
      const value = bytesFrom(sample, 'Local PAD contribution');
      if (value.length !== PAD_BYTES) throw Error('Local PAD contribution must contain 11 bytes');
      const previous = this.local.get(tick);
      if (previous && !equalBytes(previous, value))
        return this.fail('protocol', {reason: 'local input changed after publication', tick});
      // A deferred contribution remains native-local but cannot escape through
      // ACK/checksum retransmissions. Republishing it without deferSend releases
      // the hold; identical subsequent releases remain idempotent.
      if (deferSend && !previous) this.deferredInputs.add(tick);
      else if (!deferSend) this.deferredInputs.delete(tick);
      this.local.set(tick, value);
      const native = overrides.has(tick) ? bytesFrom(overrides.get(tick), 'Native PAD contribution') : value.slice();
      if (native.length !== PAD_BYTES) throw Error('Native PAD contribution must contain 11 bytes');
      const oldNative = this.nativeLocal.get(tick);
      if (oldNative && !equalBytes(oldNative, native))
        return this.fail('protocol', {reason: 'native local input changed after publication', tick});
      this.nativeLocal.set(tick, native);
    }
    while (this.local.has(this.localContiguousInput + 1)) ++this.localContiguousInput;
    await this.#pumpFrames();
    if (!deferSend) await this.#sendState();
    if (repeat) await this.#sendState();
  }

  async addChecksum(record) {
    this.#assertReady();
    const value = bytesFrom(record, 'Native checksum');
    const tick = recordTick(value);
    if (tick >= this.sourceTicks) throw Error('Native checksum is beyond the source tick bound');
    const previous = this.localChecksums.get(tick);
    if (previous && !equalBytes(previous, value)) return this.fail('protocol', {reason: 'native emitted conflicting checksums', tick});
    this.localChecksums.set(tick, value);
    while (this.localChecksums.has(this.localContiguousChecksum + 1)) ++this.localContiguousChecksum;
    await this.#compareReadyChecksums();
  }

  async setNativeProgress(cursor, {flushFinal = false} = {}) {
    this.#assertReady();
    integer(cursor, this.sourceTicks, 'native cursor');
    const through = flushFinal ? this.sourceTicks - 1 : cursor - LOCKSTEP_DELAY - 1;
    if (through > this.delayedChecksumThrough) {
      this.delayedChecksumThrough = through;
      await this.#sendState();
    }
  }

  #assertReady() {
    if (!this.ready || this.terminal) throw Error(`Lockstep peer ${this.role} is not active`);
  }

  async #sendState() {
    if (!this.ready || this.terminal) return;
    const unacknowledged = [...this.local.entries()]
      .filter(([tick]) => tick > this.remoteAckInput && !this.deferredInputs.has(tick))
      .slice(0, LOCKSTEP_MAX_BATCH)
      .map(([tick, sample]) => ({tick, pad: encodeBase64(sample)}));
    const checksums = [...this.localChecksums.entries()]
      .filter(([tick]) => tick > this.remoteAckChecksum && tick <= this.delayedChecksumThrough)
      .slice(0, LOCKSTEP_MAX_BATCH)
      .map(([tick, record]) => ({tick, record: encodeBase64(record)}));
    await this.sendObject({
      type: 'state', version: LOCKSTEP_VERSION, role: this.role,
      sequence: this.nextSequence++,
      ack_sequence: this.expectedSequence ? this.expectedSequence - 1 : null,
      ack_input: this.remoteContiguousInput >= 0 ? this.remoteContiguousInput : null,
      ack_checksum: this.remoteContiguousChecksum >= 0 ? this.remoteContiguousChecksum : null,
      unacknowledged, delayed_checksums: checksums,
    });
  }

  async #receiveState(packet, raw) {
    integer(packet.sequence, 0xffffffff, 'packet sequence');
    const rawHash = await digestText(raw);
    if (this.terminal) return;
    if (packet.sequence < this.expectedSequence) {
      const accepted = this.lastAccepted.get(packet.sequence);
      if (!accepted || accepted !== rawHash) throw Error('conflicting or expired duplicate state packet');
      return;
    }
    if (packet.sequence - this.expectedSequence > LOCKSTEP_WINDOW)
      throw Error('state packet is outside the bounded reorder window');
    const pending = this.pendingSequence.get(packet.sequence);
    if (pending && pending.rawHash !== rawHash) throw Error('conflicting state packet sequence');
    this.pendingSequence.set(packet.sequence, {packet, rawHash});
    let changed = false;
    while (this.pendingSequence.has(this.expectedSequence)) {
      const next = this.pendingSequence.get(this.expectedSequence);
      this.pendingSequence.delete(this.expectedSequence);
      this.lastAccepted.set(this.expectedSequence, next.rawHash);
      while (this.lastAccepted.size > LOCKSTEP_WINDOW) this.lastAccepted.delete(this.lastAccepted.keys().next().value);
      ++this.expectedSequence;
      changed = (await this.#applyState(next.packet)) || changed;
    }
    await this.#pumpFrames();
    if (changed) await this.#sendState();
  }

  async #applyState(packet) {
    let changed = false;
    const ackSequence = packet.ack_sequence;
    if (ackSequence !== null) {
      integer(ackSequence, 0xffffffff, 'sequence acknowledgement');
      if (ackSequence >= this.nextSequence) throw Error('peer acknowledged an unsent state packet');
    }
    for (const [field, current, countName] of [
      ['ack_input', this.remoteAckInput, 'acknowledgedInputs'],
      ['ack_checksum', this.remoteAckChecksum, 'acknowledgedChecksums'],
    ]) {
      const value = packet[field];
      if (value !== null) {
        integer(value, 0xffffffff, `${field} value`);
        const maximum = field === 'ack_input' ? this.localContiguousInput : this.localContiguousChecksum;
        if (value > maximum) throw Error(`peer ${field} exceeds published local data`);
        if (value > current) {
          this[field === 'ack_input' ? 'remoteAckInput' : 'remoteAckChecksum'] = value;
          this[countName] += value - current;
          changed = true;
        }
      }
    }
    if (!Array.isArray(packet.unacknowledged) || packet.unacknowledged.length > LOCKSTEP_MAX_BATCH ||
        !Array.isArray(packet.delayed_checksums) || packet.delayed_checksums.length > LOCKSTEP_MAX_BATCH)
      throw Error('state packet exceeded its bounded payload');
    const seenInputs = new Set();
    for (const entry of packet.unacknowledged) {
      const tick = integer(entry?.tick, this.inputTicks - 1, 'remote input tick');
      if (seenInputs.has(tick)) throw Error('state packet repeats an input tick');
      seenInputs.add(tick);
      if (tick > this.remoteContiguousInput + LOCKSTEP_WINDOW + 1)
        throw Error('remote input is outside the bounded reorder window');
      const sample = decodeSample(entry.pad);
      const old = this.remote.get(tick);
      if (old && !equalBytes(old, sample)) throw Error(`conflicting remote PAD contribution at tick ${tick}`);
      if (old) ++this.inputDuplicates;
      else { this.remote.set(tick, sample); changed = true; }
      if (tick > this.remoteContiguousInput + 1) ++this.outOfOrderInputs;
    }
    while (this.remote.has(this.remoteContiguousInput + 1)) ++this.remoteContiguousInput;

    const seenChecksums = new Set();
    for (const entry of packet.delayed_checksums) {
      const tick = integer(entry?.tick, this.sourceTicks - 1, 'remote checksum tick');
      if (seenChecksums.has(tick)) throw Error('state packet repeats a checksum tick');
      seenChecksums.add(tick);
      let record;
      try { record = decodeBase64(entry.record || ''); }
      catch { record = new Uint8Array(); }
      if (record.length !== NET_RECORD_BYTES || encodeBase64(record) !== entry.record || recordTick(record) !== tick)
        throw Error('invalid delayed checksum record');
      const old = this.remoteChecksums.get(tick);
      if (old && !equalBytes(old, record)) throw Error(`conflicting remote checksum at tick ${tick}`);
      if (old) ++this.checksumDuplicates;
      else { this.remoteChecksums.set(tick, record); changed = true; }
      if (tick > this.remoteContiguousChecksum + LOCKSTEP_WINDOW + 1)
        throw Error('remote checksum is outside the bounded reorder window');
    }
    while (this.remoteChecksums.has(this.remoteContiguousChecksum + 1)) ++this.remoteContiguousChecksum;
    await this.#compareReadyChecksums();
    return changed;
  }

  async #compareReadyChecksums() {
    // A checksum may arrive out of order. Never report a later mismatch until
    // every preceding checksum from both peers has arrived and matched.
    const through = Math.min(this.localContiguousChecksum, this.remoteContiguousChecksum);
    while (this.nextChecksumCompare <= through && !this.terminal) {
      const tick = this.nextChecksumCompare;
      const local = this.localChecksums.get(tick), remote = this.remoteChecksums.get(tick);
      if (!local || !remote) throw Error(`contiguous checksum ${tick} is missing a peer record`);
      if (!equalBytes(local, remote)) {
        const channel = mismatchChannel(local, remote);
        this.checksumMismatches.push({tick, channel});
        await this.fail('desync', {tick, channel});
        return;
      }
      ++this.nextChecksumCompare;
    }
  }

  async #pumpFrames() {
    if (!this.ready || this.terminal) return;
    this.pump = this.pump.then(async () => {
      while (this.nextSourceFrame < this.sourceTicks && !this.terminal) {
        const firstTick = this.nextSourceFrame;
        const frames = [];
        while (frames.length < LOCKSTEP_MAX_BATCH && this.nextSourceFrame < this.sourceTicks) {
          const sourceTick = this.nextSourceFrame;
          let local = zeroPad, remote = zeroPad;
          if (sourceTick >= LOCKSTEP_DELAY) {
            const inputTick = sourceTick - LOCKSTEP_DELAY;
          local = this.nativeLocal.get(inputTick);
            remote = this.remote.get(inputTick);
            if (!local || !remote) break;
          }
          const frame = new Uint8Array(NET_FRAME_BYTES);
          frame.set(noControllerPad, 22);
          frame.set(noControllerPad, 33);
          frame.set(local, this.localPort * PAD_BYTES);
          frame.set(remote, this.remotePort * PAD_BYTES);
          frames.push(frame);
        ++this.nextSourceFrame;
        }
        if (!frames.length) break;
        try {
          const batch = new Uint8Array(frames.length * NET_FRAME_BYTES);
          frames.forEach((frame, index) => batch.set(frame, index * NET_FRAME_BYTES));
          await this.pushFrame(firstTick, batch);
        } catch (error) {
          const reason = `indexed native frame push failed at source tick ${firstTick}: ${String(error.message || error)}`;
          await this.fail('protocol', {reason, tick: firstTick});
          throw Error(reason, {cause: error});
        }
      }
    });
    await this.pump;
  }

  async fail(kind, details = {}, {notify = true} = {}) {
    if (this.terminal) return;
    this.terminal = {kind, ...details};
    let callbackError = null;
    try { await this.onTerminal(this.terminal, this); }
    catch (error) { callbackError = error; }
    if (notify && this.sendRaw) {
      const packet = {type: 'terminal', version: LOCKSTEP_VERSION,
        role: this.role, kind, details: clone(details)};
      try { await this.sendRaw(JSON.stringify(packet)); }
      catch (error) { callbackError ??= error; }
    }
    if (callbackError) throw callbackError;
  }

  async disconnect(reason = 'loopback transport closed') {
    // Transport-close notification cannot be sent over the failed connection.
    await this.fail('disconnect', {reason}, {notify: false});
  }

  summary() {
    return {
      role: this.role, local_port: this.localPort, remote_port: this.remotePort,
      ready: this.ready, terminal: this.terminal,
      local_input_ticks: this.local.size,
      deferred_input_ticks: [...this.deferredInputs].sort((a, b) => a - b),
      remote_input_ticks: this.remote.size,
      native_local_divergences: [...this.local.entries()].filter(([tick, sample]) =>
        this.nativeLocal.get(tick) && !equalBytes(this.nativeLocal.get(tick), sample)).length,
      remote_contiguous_input: this.remoteContiguousInput,
      remote_ack_input: this.remoteAckInput,
      local_checksum_ticks: this.localChecksums.size,
      remote_checksum_ticks: this.remoteChecksums.size,
      remote_contiguous_checksum: this.remoteContiguousChecksum,
      next_checksum_compare: this.nextChecksumCompare,
      remote_ack_checksum: this.remoteAckChecksum,
      local_contiguous_input: this.localContiguousInput,
      local_contiguous_checksum: this.localContiguousChecksum,
      next_source_frame: this.nextSourceFrame,
      input_duplicates: this.inputDuplicates,
      checksum_duplicates: this.checksumDuplicates,
      out_of_order_inputs: this.outOfOrderInputs,
      acknowledged_inputs: this.acknowledgedInputs,
      acknowledged_checksums: this.acknowledgedChecksums,
      checksum_mismatches: this.checksumMismatches,
      start_identity_hash: this.agreementHash ?? null,
    };
  }
}

export function parseNetChecksum(record) {
  const bytes = bytesFrom(record, 'Lockstep checksum');
  const tick = recordTick(bytes);
  const view = viewOf(bytes);
  const u64 = offset => view.getBigUint64(offset, true).toString(16).padStart(16, '0');
  return {
    tick, scene: view.getUint32(4, true), seed: view.getUint32(8, true), frame: view.getUint32(12, true),
    flags: view.getUint32(16, true), objects: view.getUint32(20, true),
    input: u64(24), pad: u64(32), scene_state: u64(40), object_state: u64(48), total: u64(56),
  };
}

export const lockstepConstants = Object.freeze({
  delay: LOCKSTEP_DELAY, window: LOCKSTEP_WINDOW, maxBatch: LOCKSTEP_MAX_BATCH,
  neutralPad: toHex(zeroPad), noControllerPad: toHex(noControllerPad),
});
