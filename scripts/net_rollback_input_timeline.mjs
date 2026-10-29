import {NET_FRAME_BYTES, PAD_BYTES} from './net_lockstep_core.mjs';

export const ROLLBACK_MAX_PREDICTION_TICKS = 7;
export const ROLLBACK_DEFAULT_HISTORY_TICKS = 128;
export const ROLLBACK_DEFAULT_LOOKAHEAD_TICKS = 32;

const MAX_SAFE_TICK = Number.MAX_SAFE_INTEGER;
const MAX_HISTORY_TICKS = 4096;
const MAX_LOOKAHEAD_TICKS = 4096;
const PORT_COUNT = NET_FRAME_BYTES / PAD_BYTES;
const NO_CONTROLLER_PAD = Object.freeze([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff]);

if (!Number.isInteger(PORT_COUNT) || PORT_COUNT !== 4)
  throw Error('Rollback input timeline requires the four-port native PAD frame');

function safeTick(value, label) {
  if (!Number.isSafeInteger(value) || value < 0 || value > MAX_SAFE_TICK)
    throw Error(`${label} must be a nonnegative safe integer`);
  return value;
}

function boundedCount(value, minimum, maximum, label) {
  if (!Number.isSafeInteger(value) || value < minimum || value > maximum)
    throw Error(`${label} must be a safe integer from ${minimum} through ${maximum}`);
  return value;
}

function copyPad(value, label) {
  if (value instanceof Uint8Array) {
    if (value.byteLength !== PAD_BYTES) throw Error(`${label} must contain exactly ${PAD_BYTES} bytes`);
    return new Uint8Array(value);
  }
  if (ArrayBuffer.isView(value) && value.BYTES_PER_ELEMENT === 1) {
    if (value.byteLength !== PAD_BYTES) throw Error(`${label} must contain exactly ${PAD_BYTES} bytes`);
    return Uint8Array.from(new Uint8Array(value.buffer, value.byteOffset, value.byteLength));
  }
  if (value instanceof ArrayBuffer) {
    if (value.byteLength !== PAD_BYTES) throw Error(`${label} must contain exactly ${PAD_BYTES} bytes`);
    return Uint8Array.from(new Uint8Array(value));
  }
  if (Array.isArray(value)) {
    let valid = value.length === PAD_BYTES;
    for (let index = 0; valid && index < PAD_BYTES; ++index)
      valid = Number.isInteger(value[index]) && value[index] >= 0 && value[index] <= 255;
    if (!valid)
      throw Error(`${label} must contain exactly ${PAD_BYTES} unsigned bytes`);
    return Uint8Array.from(value);
  }
  throw Error(`${label} must be an 11-byte PAD value`);
}

function equalPad(left, right) {
  for (let index = 0; index < PAD_BYTES; ++index)
    if (left[index] !== right[index]) return false;
  return true;
}

function publicPad(pad) { return Object.freeze(Array.from(pad)); }

function nextTick(tick) {
  return tick === MAX_SAFE_TICK ? null : tick + 1;
}

function makeFrame(localPort, remotePort, localPad, remotePad) {
  const frame = Array.from({length: NET_FRAME_BYTES}, (_, index) => NO_CONTROLLER_PAD[index % PAD_BYTES]);
  const localOffset = localPort * PAD_BYTES;
  const remoteOffset = remotePort * PAD_BYTES;
  for (let index = 0; index < PAD_BYTES; ++index) {
    frame[localOffset + index] = localPad[index];
    frame[remoteOffset + index] = remotePad[index];
  }
  return Object.freeze(frame);
}

/**
 * Match-only input scheduling storage. It has no game state, state restore,
 * physics, menu, Results, or rematch lifecycle behavior.
 *
 * A future verified runtime adapter may use planForward() to execute one new
 * native tick and then call commitForward(). Before either plan kind is
 * executed, the adapter must retain the engine state at the pre-plan boundary.
 * For correction it must restore a retained state from before plan.from, then
 * execute every returned frame in order using the stored local PAD samples.
 * Call the matching commit only after execution succeeds. If execution fails
 * or the plan becomes stale before commit, restore the retained pre-plan state
 * or terminate that match; never continue from partially mutated engine state.
 * This component owns no game snapshot and commit does not restore, verify, or
 * roll back engine state. Replay acknowledgment commits input history only.
 *
 * Storage remains bounded for a long stream: local and executed rows are each
 * limited to maxHistoryTicks; remote rows are limited to retained history plus
 * maxLookaheadTicks; both configuration bounds have fixed hard ceilings.
 */
export class MatchInputTimeline {
  #startTick;
  #localPort;
  #remotePort;
  #maxHistoryTicks;
  #maxLookaheadTicks;
  #initialRemotePad;
  #remoteSeedTick;
  #remoteSeedPad;
  #localInputs;
  #remoteInputs;
  #executedInputs;
  #localThrough;
  #remoteContiguousThrough;
  #simulatedThrough;
  #dirtyFrom;
  #outstanding;
  #revision;
  #terminal;
  #disposed;

  constructor({localPort, remotePort, startTick = 0, initialRemotePad,
    maxHistoryTicks = ROLLBACK_DEFAULT_HISTORY_TICKS,
    maxLookaheadTicks = ROLLBACK_DEFAULT_LOOKAHEAD_TICKS} = {}) {
    this.#startTick = safeTick(startTick, 'Start tick');
    if (!Number.isInteger(localPort) || localPort < 0 || localPort >= PORT_COUNT ||
        !Number.isInteger(remotePort) || remotePort < 0 || remotePort >= PORT_COUNT ||
        localPort === remotePort)
      throw Error('Local and remote ports must be distinct ports from 0 through 3');
    this.#localPort = localPort;
    this.#remotePort = remotePort;
    this.#maxHistoryTicks = boundedCount(maxHistoryTicks, ROLLBACK_MAX_PREDICTION_TICKS + 1,
      MAX_HISTORY_TICKS, 'History bound');
    this.#maxLookaheadTicks = boundedCount(maxLookaheadTicks, 1, MAX_LOOKAHEAD_TICKS,
      'Inbound lookahead bound');
    // This is an explicit agreed starting prediction value, not a synthetic
    // network sample or a claim that tick startTick has been confirmed.
    this.#initialRemotePad = copyPad(initialRemotePad, 'Agreed initial remote PAD');
    this.#remoteSeedTick = this.#startTick - 1;
    this.#remoteSeedPad = this.#initialRemotePad;
    this.#localInputs = new Map();
    this.#remoteInputs = new Map();
    this.#executedInputs = new Map();
    this.#localThrough = this.#startTick - 1;
    this.#remoteContiguousThrough = this.#startTick - 1;
    this.#simulatedThrough = this.#startTick - 1;
    this.#dirtyFrom = null;
    this.#outstanding = null;
    this.#revision = 0;
    this.#terminal = null;
    this.#disposed = false;
  }

  recordLocal(tick, pad) {
    this.#assertActive();
    if (this.#outstanding) throw Error('Cannot record local input while a plan is outstanding');
    if (this.#dirtyFrom !== null) throw Error('Correction replay must be acknowledged before local input');
    safeTick(tick, 'Local tick');
    const value = copyPad(pad, 'Local PAD');
    if (tick < this.#historyFloor()) return Object.freeze({status: 'stale', tick});
    const previous = this.#localInputs.get(tick);
    if (previous) {
      if (!equalPad(previous, value)) throw Error(`Conflicting immutable local PAD at tick ${tick}`);
      return Object.freeze({status: 'duplicate', tick});
    }
    const expected = nextTick(this.#localThrough);
    const simulatedNext = nextTick(this.#simulatedThrough);
    if (expected === null || tick !== expected || tick !== simulatedNext)
      throw Error(`Local PAD tick ${tick} is not the next unsimulated tick`);
    this.#localInputs.set(tick, value);
    this.#localThrough = tick;
    ++this.#revision;
    return Object.freeze({status: 'accepted', tick});
  }

  /** Validate and accept one remote sample atomically; no frame is executed here. */
  acceptRemote(tick, pad) {
    this.#assertActive();
    safeTick(tick, 'Remote tick');
    const value = copyPad(pad, 'Remote PAD');
    if (tick < this.#startTick || tick < this.#historyFloor())
      return Object.freeze({status: 'stale', tick, earliest_changed_tick: null});
    const next = nextTick(this.#simulatedThrough);
    if (next !== null && tick >= next && tick - next >= this.#maxLookaheadTicks)
      throw Error('Remote tick exceeds bounded inbound lookahead');

    const previous = this.#remoteInputs.get(tick);
    if (previous && !equalPad(previous, value))
      throw Error(`Conflicting remote PAD at tick ${tick}`);

    // A duplicate remains idempotent in the sample store but still forms a new
    // plan boundary. Conflicting or out-of-bounds data leaves plans untouched.
    if (previous) {
      if (this.#outstanding) this.#outstanding = null;
      ++this.#revision;
      return Object.freeze({status: 'duplicate', tick, earliest_changed_tick: this.#dirtyFrom});
    }

    const candidate = new Map(this.#remoteInputs);
    candidate.set(tick, value);
    let earliestChanged = null;
    if (tick <= this.#simulatedThrough) {
      const floor = this.#historyFloor();
      for (let current = Math.max(tick, floor); current <= this.#simulatedThrough; ++current) {
        const executed = this.#executedInputs.get(current);
        if (!executed) throw Error(`Executed input history is missing retained tick ${current}`);
        if (!equalPad(executed.remotePad, this.#resolveRemote(current, candidate))) {
          earliestChanged = current;
          break;
        }
        if (current === this.#simulatedThrough) break;
      }
    }

    // Publish only after validation and correction discovery have completed.
    if (this.#outstanding) this.#outstanding = null;
    ++this.#revision;
    this.#remoteInputs = candidate;
    if (earliestChanged !== null && (this.#dirtyFrom === null || earliestChanged < this.#dirtyFrom))
      this.#dirtyFrom = earliestChanged;
    while (this.#remoteContiguousThrough < MAX_SAFE_TICK) {
      const contiguousNext = this.#remoteContiguousThrough + 1;
      if (!this.#remoteInputs.has(contiguousNext)) break;
      this.#remoteContiguousThrough = contiguousNext;
    }
    this.#trimHistory();
    return Object.freeze({status: 'accepted', tick, earliest_changed_tick: earliestChanged});
  }

  /** Build, but do not execute or commit, the next native input frame. */
  planForward() {
    this.#assertActive();
    if (this.#outstanding) throw Error('An input plan is already outstanding');
    if (this.#dirtyFrom !== null) throw Error('Correction replay must be acknowledged before forward progress');
    const tick = nextTick(this.#simulatedThrough);
    if (tick === null) return Object.freeze({status: 'blocked', reason: 'safe_tick_limit'});
    if (!this.#localInputs.has(tick))
      return Object.freeze({status: 'blocked', reason: 'local_input_missing', tick});
    const speculativeTicks = tick - this.#remoteContiguousThrough;
    if (speculativeTicks > ROLLBACK_MAX_PREDICTION_TICKS)
      return Object.freeze({status: 'blocked', reason: 'prediction_limit', tick,
        remote_contiguous_through: this.#remoteContiguousThrough});
    const remotePad = this.#resolveRemote(tick, this.#remoteInputs);
    const actual = this.#remoteInputs.has(tick);
    const record = Object.freeze({tick, localPad: this.#localInputs.get(tick), remotePad,
      remoteSource: actual ? 'actual' : 'last_known_prediction',
      speculative: tick > this.#remoteContiguousThrough});
    const plan = this.#createPlan({kind: 'forward', tick,
      frame: makeFrame(this.#localPort, this.#remotePort, record.localPad, record.remotePad),
      speculativeTicks: record.speculative ? speculativeTicks : 0,
      remoteSource: record.remoteSource});
    return plan;
  }

  /** Commit exactly the current forward plan after its native tick completed. */
  commitForward(plan) {
    this.#assertActive();
    this.#assertCurrentPlan(plan, 'forward');
    const expected = nextTick(this.#simulatedThrough);
    if (expected === null || plan.tick !== expected || this.#dirtyFrom !== null)
      throw Error('Forward plan no longer matches the simulated cursor');
    const localPad = this.#localInputs.get(plan.tick);
    const remotePad = this.#resolveRemote(plan.tick, this.#remoteInputs);
    this.#executedInputs.set(plan.tick, {localPad, remotePad});
    this.#simulatedThrough = plan.tick;
    this.#outstanding = null;
    ++this.#revision;
    this.#trimHistory();
    return this.status();
  }

  /** Build a replay plan for the complete changed suffix of executed input. */
  planReplay() {
    this.#assertActive();
    if (this.#outstanding) throw Error('An input plan is already outstanding');
    if (this.#dirtyFrom === null) throw Error('No correction replay is required');
    const from = this.#dirtyFrom;
    const through = this.#simulatedThrough;
    if (from < this.#historyFloor()) throw Error('Correction is older than retained input history');
    const frames = [];
    const resolved = [];
    for (let tick = from; tick <= through; ++tick) {
      const localPad = this.#localInputs.get(tick);
      if (!localPad) throw Error(`Original local PAD is missing from replay tick ${tick}`);
      const remotePad = this.#resolveRemote(tick, this.#remoteInputs);
      const actual = this.#remoteInputs.has(tick);
      const record = Object.freeze({tick, local_pad: publicPad(localPad),
        remote_pad: publicPad(remotePad), remote_source: actual ? 'actual' : 'last_known_prediction',
        speculative: tick > this.#remoteContiguousThrough,
        frame: makeFrame(this.#localPort, this.#remotePort, localPad, remotePad)});
      frames.push(record);
      resolved.push({tick, remotePad});
      if (tick === through) break;
    }
    const plan = this.#createPlan({kind: 'replay', from, through,
      frames: Object.freeze(frames), speculativeTicks: frames.filter(frame => frame.speculative).length});
    this.#outstanding.resolved = resolved;
    return plan;
  }

  /** Acknowledge successful adapter replay; this does not restore or verify game state. */
  commitReplay(plan) {
    this.#assertActive();
    this.#assertCurrentPlan(plan, 'replay');
    if (plan.from !== this.#dirtyFrom || plan.through !== this.#simulatedThrough)
      throw Error('Replay plan no longer matches the dirty simulated suffix');
    for (const {tick, remotePad} of this.#outstanding.resolved) {
      const localPad = this.#localInputs.get(tick);
      if (!localPad) throw Error(`Original local PAD is missing from replay tick ${tick}`);
      this.#executedInputs.set(tick, {localPad, remotePad});
    }
    this.#dirtyFrom = null;
    this.#outstanding = null;
    ++this.#revision;
    this.#trimHistory();
    return this.status();
  }

  disconnect(reason = 'remote disconnected') {
    if (this.#disposed || this.#terminal) return this.status();
    if (typeof reason !== 'string' || reason.length === 0)
      throw Error('Disconnect reason must be a nonempty string');
    this.#terminal = Object.freeze({kind: 'disconnect', reason});
    this.#outstanding = null;
    ++this.#revision;
    return this.status();
  }

  /** Release retained input data. Disposal is final and idempotent. */
  dispose() {
    if (this.#disposed) return this.status();
    this.#disposed = true;
    this.#terminal = null;
    this.#outstanding = null;
    this.#localInputs.clear();
    this.#remoteInputs.clear();
    this.#executedInputs.clear();
    this.#remoteSeedPad = null;
    this.#initialRemotePad = null;
    this.#dirtyFrom = null;
    ++this.#revision;
    return this.status();
  }

  status() {
    const simulatedConfirmed = Math.min(this.#remoteContiguousThrough, this.#simulatedThrough);
    const confirmedThrough = this.#dirtyFrom === null ? simulatedConfirmed :
      Math.min(simulatedConfirmed, this.#dirtyFrom - 1);
    return Object.freeze({
      local_port: this.#localPort, remote_port: this.#remotePort,
      start_tick: this.#startTick,
      simulated_through: this.#simulatedThrough,
      remote_contiguous_through: this.#remoteContiguousThrough,
      confirmed_through: confirmedThrough,
      dirty_from: this.#dirtyFrom,
      retained_from: this.#historyFloor(),
      local_input_count: this.#localInputs.size,
      remote_input_count: this.#remoteInputs.size,
      executed_input_count: this.#executedInputs.size,
      max_prediction_ticks: ROLLBACK_MAX_PREDICTION_TICKS,
      max_history_ticks: this.#maxHistoryTicks,
      max_lookahead_ticks: this.#maxLookaheadTicks,
      plan_outstanding: this.#outstanding !== null,
      terminal: this.#terminal,
      disposed: this.#disposed,
    });
  }

  #createPlan(fields) {
    const plan = Object.freeze({kind: fields.kind, revision: this.#revision,
      ...(fields.tick === undefined ? {} : {tick: fields.tick}),
      ...(fields.from === undefined ? {} : {from: fields.from}),
      ...(fields.through === undefined ? {} : {through: fields.through}),
      ...(fields.frame === undefined ? {} : {frame: fields.frame}),
      ...(fields.frames === undefined ? {} : {frames: fields.frames}),
      speculative_ticks: fields.speculativeTicks,
      ...(fields.remoteSource === undefined ? {} : {remote_source: fields.remoteSource})});
    this.#outstanding = {plan, revision: this.#revision, kind: fields.kind};
    return plan;
  }

  #assertCurrentPlan(plan, kind) {
    if (!this.#outstanding || this.#outstanding.plan !== plan || this.#outstanding.kind !== kind ||
        this.#outstanding.revision !== this.#revision || plan?.revision !== this.#revision)
      throw Error('Input plan is stale or was not issued by this timeline');
  }

  #resolveRemote(tick, remoteInputs) {
    let bestTick = this.#remoteSeedTick;
    let bestPad = this.#remoteSeedPad;
    for (const [candidateTick, candidatePad] of remoteInputs) {
      if (candidateTick <= tick && candidateTick > bestTick) {
        bestTick = candidateTick;
        bestPad = candidatePad;
      }
    }
    return bestPad;
  }

  #historyFloor() {
    const earliestByCapacity = Math.max(this.#startTick, this.#simulatedThrough - this.#maxHistoryTicks + 1);
    return earliestByCapacity;
  }

  #trimHistory() {
    const floor = this.#historyFloor();
    let seedTick = this.#remoteSeedTick;
    let seedPad = this.#remoteSeedPad;
    for (const [tick, pad] of this.#remoteInputs) {
      if (tick < floor && tick > seedTick) {
        seedTick = tick;
        seedPad = pad;
      }
    }
    this.#remoteSeedTick = seedTick;
    this.#remoteSeedPad = seedPad;
    for (const tick of this.#localInputs.keys()) if (tick < floor) this.#localInputs.delete(tick);
    for (const tick of this.#executedInputs.keys()) if (tick < floor) this.#executedInputs.delete(tick);
    for (const tick of this.#remoteInputs.keys()) if (tick < floor) this.#remoteInputs.delete(tick);
  }

  #assertActive() {
    if (this.#disposed) throw Error('Input timeline is disposed');
    if (this.#terminal) throw Error(`Input timeline is terminal: ${this.#terminal.kind}`);
  }
}
