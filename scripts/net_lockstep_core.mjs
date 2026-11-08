export const LOCKSTEP_VERSION = 1;
export const LOCKSTEP_DELAY = 2;
export const LOCKSTEP_MAX_SOURCE_TICKS = 216000;
export const LOCKSTEP_STREAM_MAX_INPUT_TICKS = LOCKSTEP_MAX_SOURCE_TICKS - LOCKSTEP_DELAY;
export const LOCKSTEP_WINDOW = 32;
export const LOCKSTEP_MAX_BATCH = 32;
export const LOCKSTEP_MAX_PENDING_RECEIVE_MESSAGES = 64;
export const LOCKSTEP_MAX_PENDING_RECEIVE_BYTES = 1024 * 1024;
export const NET_RECORD_BYTES = 64;
export const PAD_BYTES = 11;
export const NET_FRAME_BYTES = 44;
export const TERMINAL = Object.freeze({desync: 1, disconnect: 2, protocol: 3, startIdentity: 4});

/** Return a frozen suffix without reading or copying the retained prefix. */
export function readOnlySuffix(rows, first, label = 'Retained rows') {
  if (!Array.isArray(rows) || !Number.isSafeInteger(first) || first < 0 || first > rows.length)
    throw Error(`${label} offset must be a nonnegative safe integer within retained history`);
  return Object.freeze(rows.slice(first));
}

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

function deferred() {
  let resolve, reject;
  const promise = new Promise((resolvePromise, rejectPromise) => {
    resolve = resolvePromise;
    reject = rejectPromise;
  });
  promise.catch(() => {});
  return {promise, resolve, reject};
}

function exactPacketKeys(packet, keys, label) {
  if (!packet || typeof packet !== 'object' || Array.isArray(packet) ||
      Object.keys(packet).sort().join(',') !== [...keys].sort().join(','))
    throw Error(`Invalid lockstep ${label} fields`);
}

// MatchOutcome in melee/gm/forward.h; gameplay_match_rules.c publishes a
// single active slot only for timeout or elimination, otherwise -1.
export function validateMatchFinalOutcome(outcome, winner) {
  if (!Number.isInteger(outcome) || outcome < 1 || outcome > 9 ||
      !Number.isInteger(winner) || winner < -1 || winner > 3 ||
      (winner !== -1 && outcome !== 1 && outcome !== 2))
    throw Error('Final checksum requires an exact native outcome and winner');
}

function sameMatchFinal(left, right) {
  return !!left && !!right && left.scope === right.scope &&
    left.agreementHash === right.agreementHash &&
    left.inputOrdinal === right.inputOrdinal && left.sourceFrame === right.sourceFrame &&
    left.outcome === right.outcome && left.winner === right.winner &&
    equalBytes(left.checksum, right.checksum);
}

function finalMismatchDetails(local, remote) {
  const reason = local.inputOrdinal !== remote.inputOrdinal
    ? 'final consumed input ordinal differs'
    : local.sourceFrame !== remote.sourceFrame
      ? 'final source frame differs'
      : local.outcome !== remote.outcome
        ? 'final native outcome differs'
        : local.winner !== remote.winner
          ? 'final native winner differs'
          : 'final native checksum bytes differ';
  const channel = equalBytes(local.checksum, remote.checksum)
    ? null : mismatchChannel(local.checksum, remote.checksum);
  return {reason, input_ordinal: local.inputOrdinal, remote_input_ordinal: remote.inputOrdinal,
    source_frame: local.sourceFrame, remote_source_frame: remote.sourceFrame,
    outcome: local.outcome, remote_outcome: remote.outcome,
    winner: local.winner, remote_winner: remote.winner,
    ...(channel === null ? {} : {channel})};
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
  constructor({role, sourceTicks, inputTicks, pushFrame, onTerminal = () => {}, onReady = () => {},
    onRemoteContributionBatch = () => {},
    isClosedSendCancellation = () => false, isClosedDisconnect = () => false,
    onSendCancellation = () => {}, streaming = false}) {
    if (!(role in opposite)) throw Error('Lockstep role must be alpha or beta');
    this.role = role;
    this.remoteRole = opposite[role];
    this.localPort = portFor[role];
    this.remotePort = portFor[this.remoteRole];
    this.sourceTicks = integer(sourceTicks, LOCKSTEP_MAX_SOURCE_TICKS, 'source tick bound');
    this.inputTicks = integer(inputTicks, LOCKSTEP_MAX_SOURCE_TICKS, 'input tick bound');
    if (this.sourceTicks < LOCKSTEP_DELAY || this.inputTicks + LOCKSTEP_DELAY !== this.sourceTicks)
      throw Error('Lockstep input and source tick bounds must differ by the fixed two-tick delay');
    if (typeof streaming !== 'boolean') throw Error('Lockstep streaming mode must be boolean');
    this.streaming = streaming;
    this.pushFrame = pushFrame;
    this.onTerminal = onTerminal;
    this.onReady = onReady;
    if (typeof isClosedSendCancellation !== 'function' || typeof isClosedDisconnect !== 'function' ||
        typeof onSendCancellation !== 'function' || typeof onRemoteContributionBatch !== 'function')
      throw Error('Lockstep lifecycle and contribution hooks must be functions');
    this.isClosedSendCancellation = isClosedSendCancellation;
    this.isClosedDisconnect = isClosedDisconnect;
    this.onSendCancellation = onSendCancellation;
    this.onRemoteContributionBatch = onRemoteContributionBatch;
    this.sendRaw = null;
    this.localHello = null;
    this.startAttempted = false;
    this.startPreparation = null;
    this.remoteHello = null;
    this.ready = false;
    this.terminal = null;
    this.local = new Map();
    this.nativeLocal = new Map();
    this.nativeLocalDivergences = 0;
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
    // `expectedSequence` tracks ordered packets being processed. A packet is
    // advertised as acknowledged only after its remote contribution observer
    // has accepted every newly received PAD row.
    this.admittedSequence = -1;
    this.remoteAckSequence = -1;
    this.ackAdvertisedBySequence = new Map();
    this.remoteAckAdvertisedInput = -1;
    this.remoteAckAdvertisedChecksum = -1;
    this.remoteInputRetiredThrough = -1;
    this.remoteChecksumRetiredThrough = -1;
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
    this.matchFinalEnabled = false;
    this.localMatchFinal = null;
    this.remoteMatchFinal = null;
    this.localMatchFinalSendStarted = false;
    this.localMatchFinalSendSettled = false;
    this.localMatchFinalAckSendSettled = false;
    this.localMatchFinalAckSend = null;
    this.matchFinalAckRetryPending = false;
    this.remoteMatchFinalAck = null;
    this.matchFinalCompletion = deferred();
    this.matchFinalSendQueue = Promise.resolve();
    this.matchFinalDriveQueued = false;
  }

  attach(sendRaw) { this.sendRaw = sendRaw; }

  async sendObject(object) {
    if (!this.sendRaw || this.terminal) return;
    await this.sendRaw(JSON.stringify(object));
  }

  async start(agreement) {
    if (this.startAttempted) throw Error('Lockstep start identity was already sent');
    this.startAttempted = true;
    if (agreement?.match_final !== undefined && agreement.match_final !== 1)
      throw Error('Lockstep match_final capability must be exactly 1 when present');
    if (agreement?.streaming !== undefined && agreement.streaming !== 1)
      throw Error('Lockstep streaming capability must be exactly 1 when present');
    if (this.streaming !== (agreement?.streaming === 1) ||
        (this.streaming && agreement?.match_final !== 1))
      throw Error('Lockstep stream mode must be explicitly agreed with final checksum exchange');
    this.matchFinalEnabled = agreement?.match_final === 1;
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
      if (packet?.type === 'match_final' || packet?.type === 'match_final_ack') {
        if (!this.ready) throw Error('final checksum arrived before peer identity agreement');
        return await this.#receiveMatchFinal(packet);
      }
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
    if (this.localMatchFinal)
      throw Error('Lockstep final checksum was published; new local PAD publication is closed');
    if (this.streaming && deferSend)
      throw Error('Streaming match mode does not permit deferred local PAD publication');
    if (!Array.isArray(entries) || !entries.length || entries.length > LOCKSTEP_MAX_BATCH)
      throw Error(`Lockstep local input batches must contain 1..${LOCKSTEP_MAX_BATCH} contributions`);
    const seen = new Set();
    const overrides = new Map(nativeOverrides);
    for (const [tick, sample] of entries) {
      if (this.streaming && Number.isSafeInteger(tick) && tick >= this.inputTicks) {
        const error = Error(`Streaming input capacity ${this.inputTicks} was exhausted before native terminal`);
        await this.fail('protocol', {reason: 'stream_input_capacity_exhausted',
          input_capacity: this.inputTicks, local_contiguous_input: this.localContiguousInput});
        throw error;
      }
      integer(tick, this.inputTicks - 1, 'input tick');
      if (seen.has(tick)) throw Error('Local input batch repeats an input tick');
      seen.add(tick);
      const value = bytesFrom(sample, 'Local PAD contribution');
      if (value.length !== PAD_BYTES) throw Error('Local PAD contribution must contain 11 bytes');
      const previous = this.local.get(tick);
      if (this.streaming && !previous && tick <= this.localContiguousInput) {
        const error = Error(`Streaming local PAD ${tick} was already retired`);
        await this.fail('protocol', {reason: 'stream_local_pad_republished_after_retirement', tick});
        throw error;
      }
      if (this.streaming && !previous &&
          (this.local.size >= LOCKSTEP_WINDOW || this.nativeLocal.size >= LOCKSTEP_WINDOW)) {
        const error = Error(`Streaming local PAD retention reached its ${LOCKSTEP_WINDOW}-row protocol window`);
        await this.fail('protocol', {reason: 'stream_local_retention_window_exhausted',
          local_rows: this.local.size, native_local_rows: this.nativeLocal.size});
        throw error;
      }
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
      const wasDivergent = oldNative !== undefined && previous !== undefined && !equalBytes(oldNative, previous);
      const isDivergent = !equalBytes(native, value);
      this.nativeLocalDivergences += Number(isDivergent) - Number(wasDivergent);
      this.nativeLocal.set(tick, native);
    }
    while (this.local.has(this.localContiguousInput + 1)) ++this.localContiguousInput;
    await this.#pumpFrames();
    this.#pruneStreamingRows();
    if (!deferSend) {
      const outcome = await this.#sendState({context: 'local_input', input_ticks: entries.map(([tick]) => tick)});
      if (outcome?.status === 'cancelled_unpublished') return outcome;
    }
    if (repeat) return this.#sendState({context: 'local_input', input_ticks: entries.map(([tick]) => tick)});
  }

  async addChecksum(record) {
    this.#assertReady();
    const value = bytesFrom(record, 'Native checksum');
    const tick = recordTick(value);
    if (tick >= this.sourceTicks) throw Error('Native checksum is beyond the source tick bound');
    const previous = this.localChecksums.get(tick);
    if (this.streaming && !previous && this.localChecksums.size >= LOCKSTEP_WINDOW) {
      const error = Error(`Streaming local checksum retention reached its ${LOCKSTEP_WINDOW}-row protocol window`);
      await this.fail('protocol', {reason: 'stream_local_checksum_window_exhausted',
        retained_rows: this.localChecksums.size});
      throw error;
    }
    if (previous && !equalBytes(previous, value)) return this.fail('protocol', {reason: 'native emitted conflicting checksums', tick});
    this.localChecksums.set(tick, value);
    while (this.localChecksums.has(this.localContiguousChecksum + 1)) ++this.localContiguousChecksum;
    await this.#compareReadyChecksums();
    this.#pruneStreamingRows();
  }

  async publishMatchFinal({inputOrdinal, sourceFrame, checksum, outcome, winner} = {}) {
    this.#assertReady();
    if (!this.matchFinalEnabled)
      throw Error('Lockstep match_final capability was not agreed');
    inputOrdinal = integer(inputOrdinal, this.inputTicks - 1, 'final input ordinal');
    sourceFrame = integer(sourceFrame, 0xffffffff, 'final source frame');
    validateMatchFinalOutcome(outcome, winner);
    const value = bytesFrom(checksum, 'Final native checksum');
    if (value.length !== NET_RECORD_BYTES || recordTick(value) !== inputOrdinal ||
        viewOf(value).getUint32(12, true) !== sourceFrame)
      throw Error('Final native checksum does not match its actual input ordinal and source frame');
    if (this.localContiguousInput < inputOrdinal || this.remoteContiguousInput < inputOrdinal)
      throw Error('Final checksum requires actual local and remote PAD prefixes through its ordinal');
    const final = Object.freeze({scope: this.localHello.agreement.scope,
      agreementHash: this.agreementHash, inputOrdinal, sourceFrame, outcome, winner, checksum: value});
    if (this.localMatchFinal && !sameMatchFinal(this.localMatchFinal, final)) {
      await this.fail('protocol', {reason: 'conflicting local final checksum publication',
        input_ordinal: inputOrdinal, source_frame: sourceFrame});
      throw Error('Conflicting local final checksum publication');
    }
    this.localMatchFinal ??= final;
    const packet = this.#matchFinalPacket('match_final', this.role, this.localMatchFinal);
    const send = this.matchFinalSendQueue.then(async () => {
      this.localMatchFinalSendStarted = true;
      await this.#sendMatchFinalObject(packet);
      this.localMatchFinalSendSettled = true;
      await this.#driveMatchFinal();
    });
    this.matchFinalSendQueue = send.catch(() => {});
    try { await send; }
    catch (error) {
      if (!this.terminal)
        await this.fail('protocol', {reason: `final checksum send failed: ${String(error?.message || error)}`});
      throw error;
    }
    if (this.terminal) throw Error(`Lockstep final checksum ended with ${this.terminal.kind}`);
    return this.matchFinalCompletion.promise;
  }

  async setNativeProgress(cursor, {flushFinal = false} = {}) {
    this.#assertReady();
    if (this.localMatchFinal)
      throw Error('Lockstep final checksum was published; native progress is closed');
    integer(cursor, this.sourceTicks, 'native cursor');
    const through = flushFinal ? this.sourceTicks - 1 : cursor - LOCKSTEP_DELAY - 1;
    if (through > this.delayedChecksumThrough) {
      this.delayedChecksumThrough = through;
      return this.#sendState({context: 'native_progress', source_cursor: cursor, flush_final: flushFinal});
    }
  }

  #assertReady() {
    if (!this.ready || this.terminal) throw Error(`Lockstep peer ${this.role} is not active`);
  }

  async #sendState(context = {}) {
    if (!this.ready) return;
    if (this.terminal) {
      if (this.terminal.kind === 'disconnect' && context.context === 'local_input' &&
          this.isClosedDisconnect()) {
        // The accepted local map row remains; no state packet for this call was built.
        const outcome = Object.freeze({status: 'cancelled_unpublished',
          reason: 'peer_disconnected_before_state_send', context: context.context,
          packet_type: 'state', publication_scope: 'no_state_packet_attempt', sequence: null,
          input_ticks: Object.freeze(context.input_ticks ? [...context.input_ticks] : []),
          packet_input_ticks: Object.freeze([]), checksum_ticks: Object.freeze([]),
          ack_sequence: null, ack_input: null, ack_checksum: null});
        this.onSendCancellation(outcome);
        return outcome;
      }
      return;
    }
    if (this.streaming && this.nextSequence >= 0xffffffff) {
      const error = Error('Streaming state packet sequence capacity exhausted before native terminal');
      await this.fail('protocol', {reason: 'stream_state_sequence_exhausted',
        next_sequence: this.nextSequence});
      throw error;
    }
    if (this.streaming && this.nextSequence - (this.remoteAckSequence + 1) >= LOCKSTEP_WINDOW) {
      const error = Error(`Streaming state ACKs exceeded the ${LOCKSTEP_WINDOW}-packet protocol window`);
      await this.fail('protocol', {reason: 'stream_state_ack_window_exhausted',
        next_sequence: this.nextSequence, remote_ack_sequence: this.remoteAckSequence});
      throw error;
    }
    const unacknowledged = [...this.local.entries()]
      .filter(([tick]) => tick > this.remoteAckInput && !this.deferredInputs.has(tick))
      .slice(0, LOCKSTEP_MAX_BATCH)
      .map(([tick, sample]) => ({tick, pad: encodeBase64(sample)}));
    const checksums = [...this.localChecksums.entries()]
      .filter(([tick]) => tick > this.remoteAckChecksum && tick <= this.delayedChecksumThrough)
      .slice(0, LOCKSTEP_MAX_BATCH)
      .map(([tick, record]) => ({tick, record: encodeBase64(record)}));
    const packet = {
      type: 'state', version: LOCKSTEP_VERSION, role: this.role,
      sequence: this.nextSequence++,
      ack_sequence: this.admittedSequence >= 0 ? this.admittedSequence : null,
      ack_input: this.remoteContiguousInput >= 0 ? this.remoteContiguousInput : null,
      ack_checksum: this.remoteContiguousChecksum >= 0 ? this.remoteContiguousChecksum : null,
      unacknowledged, delayed_checksums: checksums,
    };
    if (this.streaming) {
      this.ackAdvertisedBySequence.set(packet.sequence, Object.freeze({
        input:this.remoteContiguousInput, checksum:this.remoteContiguousChecksum,
      }));
    }
    try { await this.sendObject(packet); }
    catch (error) {
      if (this.ready && (!this.terminal || this.terminal.kind === 'disconnect') &&
          this.isClosedSendCancellation(error)) {
        // This identifies one failed packet attempt, not whether another packet
        // already carried any of its unacknowledged inputs or checksums.
        const outcome = Object.freeze({status: 'cancelled_unpublished', context: context.context ?? 'state',
          reason: 'closed_transport_send', packet_type: packet.type,
          publication_scope: 'this_state_packet_attempt_only', sequence: packet.sequence,
          input_ticks: Object.freeze(context.input_ticks ? [...context.input_ticks] : []),
          packet_input_ticks: Object.freeze(unacknowledged.map(entry => entry.tick)),
          checksum_ticks: Object.freeze(checksums.map(entry => entry.tick)),
          ack_sequence: packet.ack_sequence, ack_input: packet.ack_input,
          ack_checksum: packet.ack_checksum,
          ...(context.source_cursor === undefined ? {} : {source_cursor: context.source_cursor}),
          ...(context.flush_final === undefined ? {} : {flush_final: context.flush_final})});
        this.onSendCancellation(outcome);
        return outcome;
      }
      throw error;
    }
  }

  async #receiveState(packet, raw) {
    integer(packet.sequence, 0xffffffff, 'packet sequence');
    if (this.streaming && packet.sequence === 0xffffffff)
      throw Error('Streaming state packet sequence capacity exhausted before native terminal');
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
      const applied = await this.#applyState(next.packet);
      changed = applied.changed || changed;
      if (this.terminal) return;
      if (applied.remoteInputs.length) {
        const inputs = Object.freeze(applied.remoteInputs.map(({tick, pad}) => Object.freeze({
          tick, pad: Object.freeze(Array.from(pad)),
        })));
        await this.onRemoteContributionBatch(Object.freeze({
          protocolVersion: LOCKSTEP_VERSION,
          agreementHash: this.agreementHash,
          senderRole: this.remoteRole,
          remotePort: this.remotePort,
          packetSequence: next.packet.sequence,
          inputs,
        }));
        // #applyState stages new remote rows rather than exposing them in the
        // pump/ACK maps. Commit only after the page observer has accepted them.
        if (this.terminal) return;
        for (const {tick, pad} of applied.remoteInputs) this.remote.set(tick, pad);
        while (this.remote.has(this.remoteContiguousInput + 1)) ++this.remoteContiguousInput;
      }
      this.admittedSequence = next.packet.sequence;
    }
    await this.#pumpFrames();
    this.#pruneStreamingRows();
    if (this.matchFinalEnabled) this.#queueMatchFinalDrive();
    if (changed) return this.#sendState({context: 'inbound_state_ack'});
  }

  #matchFinalPacket(type, role, final) {
    return {type, version: LOCKSTEP_VERSION, role, scope: final.scope,
      agreement_hash: final.agreementHash, input_ordinal: final.inputOrdinal,
      source_frame: final.sourceFrame, outcome: final.outcome, winner: final.winner,
      checksum: encodeBase64(final.checksum)};
  }

  #decodeMatchFinalPacket(packet, type) {
    exactPacketKeys(packet, ['type', 'version', 'role', 'scope', 'agreement_hash',
      'input_ordinal', 'source_frame', 'outcome', 'winner', 'checksum'], 'final checksum');
    if (packet.type !== type || packet.version !== LOCKSTEP_VERSION ||
        packet.role !== this.remoteRole || packet.scope !== this.localHello.agreement.scope ||
        packet.agreement_hash !== this.agreementHash)
      throw Error('Final checksum identity, scope, role, or agreement differs');
    const inputOrdinal = integer(packet.input_ordinal, this.inputTicks - 1, 'final input ordinal');
    const sourceFrame = integer(packet.source_frame, 0xffffffff, 'final source frame');
    const {outcome, winner} = packet;
    validateMatchFinalOutcome(outcome, winner);
    if (typeof packet.checksum !== 'string' ||
        !/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(packet.checksum))
      throw Error('Invalid final checksum encoding');
    let checksum;
    try { checksum = decodeBase64(packet.checksum); }
    catch { throw Error('Invalid final checksum encoding'); }
    if (checksum.length !== NET_RECORD_BYTES || encodeBase64(checksum) !== packet.checksum ||
        recordTick(checksum) !== inputOrdinal || viewOf(checksum).getUint32(12, true) !== sourceFrame)
      throw Error('Final checksum record does not match its actual input ordinal and source frame');
    return Object.freeze({scope: packet.scope, agreementHash: packet.agreement_hash,
      inputOrdinal, sourceFrame, outcome, winner, checksum});
  }

  async #receiveMatchFinal(packet) {
    if (!this.matchFinalEnabled)
      throw Error('Final checksum packet arrived without the agreed match_final capability');
    const isAck = packet.type === 'match_final_ack';
    const decoded = this.#decodeMatchFinalPacket(packet, isAck ? 'match_final_ack' : 'match_final');
    if (isAck) {
      if (!this.localMatchFinalSendStarted || !this.localMatchFinal || !this.remoteMatchFinal)
        throw Error('Final checksum ACK arrived before local send start or remote final record');
      if (!sameMatchFinal(this.localMatchFinal, decoded))
        throw Error('Final checksum ACK does not echo the exact matched record');
      if (this.remoteMatchFinalAck && !sameMatchFinal(this.remoteMatchFinalAck, decoded))
        throw Error('Conflicting final checksum ACK retransmission');
      this.remoteMatchFinalAck ??= decoded;
      this.#completeMatchFinalIfReady();
      return;
    }
    if (this.remoteMatchFinal) {
      if (!sameMatchFinal(this.remoteMatchFinal, decoded))
        throw Error('Conflicting final checksum retransmission');
      this.matchFinalAckRetryPending = true;
    }
    this.remoteMatchFinal ??= decoded;
    if (this.localMatchFinal && !sameMatchFinal(this.localMatchFinal, this.remoteMatchFinal)) {
      await this.fail('desync', finalMismatchDetails(this.localMatchFinal, this.remoteMatchFinal));
      return;
    }
    this.#queueMatchFinalDrive();
  }

  #queueMatchFinalDrive() {
    if (!this.matchFinalEnabled || this.matchFinalDriveQueued || this.terminal) return;
    this.matchFinalDriveQueued = true;
    queueMicrotask(() => {
      this.matchFinalDriveQueued = false;
      this.#driveMatchFinal().catch(error => {
        if (!this.terminal) return this.fail('protocol', {
          reason: `final checksum acknowledgement failed: ${String(error?.message || error)}`}).catch(() => {});
      });
    });
  }

  async #driveMatchFinal() {
    if (!this.matchFinalEnabled || this.terminal || !this.localMatchFinal ||
        !this.localMatchFinalSendSettled || !this.remoteMatchFinal) return;
    if (this.localContiguousInput < this.localMatchFinal.inputOrdinal ||
        this.remoteContiguousInput < this.localMatchFinal.inputOrdinal) return;
    if (!sameMatchFinal(this.localMatchFinal, this.remoteMatchFinal)) {
      await this.fail('desync', finalMismatchDetails(this.localMatchFinal, this.remoteMatchFinal));
      return;
    }
    if (!this.localMatchFinalAckSendSettled || this.matchFinalAckRetryPending) {
      if (this.localMatchFinalAckSend) return;
      this.matchFinalAckRetryPending = false;
      const packet = this.#matchFinalPacket('match_final_ack', this.role, this.remoteMatchFinal);
      const send = this.#sendMatchFinalObject(packet).then(() => {
        this.localMatchFinalAckSendSettled = true;
        this.#completeMatchFinalIfReady();
      }).catch(async error => {
        if (!this.terminal)
          await this.fail('protocol', {reason: `final checksum ACK send failed: ${String(error?.message || error)}`});
        throw error;
      });
      this.localMatchFinalAckSend = send;
      send.finally(() => {
        if (this.localMatchFinalAckSend === send) this.localMatchFinalAckSend = null;
        if (this.matchFinalAckRetryPending) this.#queueMatchFinalDrive();
      }).catch(() => {});
      await send;
    }
  }

  #completeMatchFinalIfReady() {
    if (this.terminal || !this.localMatchFinal || !this.remoteMatchFinal ||
        !sameMatchFinal(this.localMatchFinal, this.remoteMatchFinal) ||
        !this.localMatchFinalSendSettled || !this.localMatchFinalAckSendSettled ||
        !this.remoteMatchFinalAck) return;
    this.matchFinalCompletion.resolve(Object.freeze({input_ordinal: this.localMatchFinal.inputOrdinal,
      source_frame: this.localMatchFinal.sourceFrame, agreement_hash: this.agreementHash,
      role: this.role, outcome: this.localMatchFinal.outcome,
      winner: this.localMatchFinal.winner, matched_bytes: NET_RECORD_BYTES}));
  }

  async #sendMatchFinalObject(packet) {
    if (this.terminal) throw Error(`Lockstep peer already ended with ${this.terminal.kind}`);
    const sendRaw = this.sendRaw;
    if (typeof sendRaw !== 'function') throw Error('Lockstep final checksum has no attached transport');
    await sendRaw(JSON.stringify(packet));
    if (this.terminal) throw Error(`Lockstep peer ended with ${this.terminal.kind} during final checksum send`);
  }

  async #applyState(packet) {
    let changed = false;
    const remoteInputs = [];
    const ackSequence = packet.ack_sequence;
    if (ackSequence !== null) {
      integer(ackSequence, 0xffffffff, 'sequence acknowledgement');
      if (ackSequence >= this.nextSequence) throw Error('peer acknowledged an unsent state packet');
      if (this.streaming && ackSequence > this.remoteAckSequence) {
        const advertised = this.ackAdvertisedBySequence.get(ackSequence);
        if (!advertised) throw Error('peer acknowledged an expired streaming state watermark');
        this.remoteAckSequence = ackSequence;
        this.remoteAckAdvertisedInput = Math.max(this.remoteAckAdvertisedInput, advertised.input);
        this.remoteAckAdvertisedChecksum = Math.max(this.remoteAckAdvertisedChecksum, advertised.checksum);
        for (const sequence of this.ackAdvertisedBySequence.keys()) {
          if (sequence <= ackSequence) this.ackAdvertisedBySequence.delete(sequence);
        }
      }
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
      if (this.streaming && tick <= this.remoteInputRetiredThrough)
        throw Error(`Remote PAD tick ${tick} arrived after its acknowledged retirement`);
      if (seenInputs.has(tick)) throw Error('state packet repeats an input tick');
      seenInputs.add(tick);
      if (tick > this.remoteContiguousInput + LOCKSTEP_WINDOW + 1)
        throw Error('remote input is outside the bounded reorder window');
      const sample = decodeSample(entry.pad);
      const old = this.remote.get(tick);
      if (old && !equalBytes(old, sample)) throw Error(`conflicting remote PAD contribution at tick ${tick}`);
      if (old) ++this.inputDuplicates;
      else {
        if (this.streaming && this.remote.size + remoteInputs.length >= LOCKSTEP_WINDOW)
          throw Error(`Streaming remote PAD retention exceeded its ${LOCKSTEP_WINDOW}-row protocol window`);
        remoteInputs.push({tick, pad: sample}); changed = true;
      }
      if (tick > this.remoteContiguousInput + 1) ++this.outOfOrderInputs;
    }

    const seenChecksums = new Set();
    for (const entry of packet.delayed_checksums) {
      const tick = integer(entry?.tick, this.sourceTicks - 1, 'remote checksum tick');
      if (this.streaming && tick <= this.remoteChecksumRetiredThrough)
        throw Error(`Remote checksum tick ${tick} arrived after its acknowledged retirement`);
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
      else {
        if (this.streaming && this.remoteChecksums.size >= LOCKSTEP_WINDOW)
          throw Error(`Streaming remote checksum retention exceeded its ${LOCKSTEP_WINDOW}-row protocol window`);
        this.remoteChecksums.set(tick, record); changed = true;
      }
      if (tick > this.remoteContiguousChecksum + LOCKSTEP_WINDOW + 1)
        throw Error('remote checksum is outside the bounded reorder window');
    }
    while (this.remoteChecksums.has(this.remoteContiguousChecksum + 1)) ++this.remoteContiguousChecksum;
    await this.#compareReadyChecksums();
    return {changed, remoteInputs};
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
    this.#pruneStreamingRows();
  }

  #pruneStreamingRows() {
    if (!this.streaming) return;
    for (const tick of this.local.keys()) {
      if (tick <= this.remoteAckInput) this.local.delete(tick);
    }
    const nativeInputThrough = Math.min(this.localContiguousInput, this.remoteContiguousInput,
      this.nextSourceFrame - LOCKSTEP_DELAY - 1);
    for (const tick of this.nativeLocal.keys())
      if (tick <= nativeInputThrough) this.nativeLocal.delete(tick);
    const remoteInputThrough = Math.min(nativeInputThrough, this.remoteAckAdvertisedInput);
    for (const tick of this.remote.keys()) {
      if (tick <= remoteInputThrough) {
        this.remote.delete(tick);
        if (tick > this.remoteInputRetiredThrough) this.remoteInputRetiredThrough = tick;
      }
    }
    const localChecksumThrough = Math.min(this.nextChecksumCompare - 1, this.remoteAckChecksum);
    for (const tick of this.localChecksums.keys()) {
      if (tick <= localChecksumThrough) this.localChecksums.delete(tick);
    }
    const remoteChecksumThrough = Math.min(this.nextChecksumCompare - 1,
      this.remoteAckAdvertisedChecksum);
    for (const tick of this.remoteChecksums.keys()) {
      if (tick <= remoteChecksumThrough) {
        this.remoteChecksums.delete(tick);
        if (tick > this.remoteChecksumRetiredThrough) this.remoteChecksumRetiredThrough = tick;
      }
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
    if (this.streaming) this.ackAdvertisedBySequence.clear();
    this.matchFinalCompletion.reject(Error(`Lockstep peer reached ${kind} before final checksum agreement`));
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
    const result = {
      role: this.role, local_port: this.localPort, remote_port: this.remotePort,
      ready: this.ready, terminal: this.terminal,
      local_input_ticks: this.streaming ? this.localContiguousInput + 1 : this.local.size,
      deferred_input_ticks: [...this.deferredInputs].sort((a, b) => a - b),
      remote_input_ticks: this.streaming ? this.remoteContiguousInput + 1 : this.remote.size,
      native_local_divergences: this.streaming ? this.nativeLocalDivergences :
        [...this.local.entries()].filter(([tick, sample]) =>
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
    if (this.streaming) Object.assign(result, {streaming: true,
      retained_local_input_rows: this.local.size,
      retained_native_local_rows: this.nativeLocal.size,
      retained_remote_input_rows: this.remote.size,
      retained_local_checksum_rows: this.localChecksums.size,
      retained_remote_checksum_rows: this.remoteChecksums.size,
      retained_ack_watermarks: this.ackAdvertisedBySequence.size,
      remote_ack_advertised_input: this.remoteAckAdvertisedInput,
      remote_ack_advertised_checksum: this.remoteAckAdvertisedChecksum,
      remote_ack_sequence: this.remoteAckSequence});
    return result;
  }

  /** Compact read-only counters for hot health paths; full arrays stay in summary(). */
  health() {
    const result = {
      role: this.role, local_port: this.localPort, remote_port: this.remotePort,
      ready: this.ready, terminal: this.terminal ? Object.freeze({...this.terminal}) : null,
      local_input_ticks: this.streaming ? this.localContiguousInput + 1 : this.local.size,
      deferred_input_count: this.deferredInputs.size,
      remote_input_ticks: this.streaming ? this.remoteContiguousInput + 1 : this.remote.size,
      native_local_divergences: this.nativeLocalDivergences,
      remote_contiguous_input: this.remoteContiguousInput, remote_ack_input: this.remoteAckInput,
      local_checksum_ticks: this.localChecksums.size,
      remote_checksum_ticks: this.remoteChecksums.size,
      remote_contiguous_checksum: this.remoteContiguousChecksum,
      next_checksum_compare: this.nextChecksumCompare, remote_ack_checksum: this.remoteAckChecksum,
      local_contiguous_input: this.localContiguousInput,
      local_contiguous_checksum: this.localContiguousChecksum,
      next_source_frame: this.nextSourceFrame,
      input_duplicates: this.inputDuplicates, checksum_duplicates: this.checksumDuplicates,
      out_of_order_inputs: this.outOfOrderInputs, acknowledged_inputs: this.acknowledgedInputs,
      acknowledged_checksums: this.acknowledgedChecksums,
      checksum_mismatch_count: this.checksumMismatches.length,
      start_identity_hash: this.agreementHash ?? null,
    };
    if (this.streaming) Object.assign(result, {streaming: true,
      retained_local_input_rows: this.local.size,
      retained_native_local_rows: this.nativeLocal.size,
      retained_remote_input_rows: this.remote.size,
      retained_local_checksum_rows: this.localChecksums.size,
      retained_remote_checksum_rows: this.remoteChecksums.size,
      retained_ack_watermarks: this.ackAdvertisedBySequence.size,
      remote_ack_advertised_input: this.remoteAckAdvertisedInput,
      remote_ack_advertised_checksum: this.remoteAckAdvertisedChecksum,
      remote_ack_sequence: this.remoteAckSequence});
    return Object.freeze(result);
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
