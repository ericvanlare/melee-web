export const FIRST_PAIR_NATIVE_FILE_OPERATIONS = Object.freeze([
  'native.hps.begin', 'native.hps.select-file', 'native.hps.request',
  'native.hps.cancel', 'native.hps.pump', 'native.hps.end',
  'native.hps.io-create', 'native.hps.io-add-file', 'native.hps.io-add-buffer',
  'native.hps.io-remove', 'native.hps.io-submit', 'native.hps.io-cancel',
  'native.hps.io-pump', 'native.hps.io-destroy',
  'native.ssm.begin', 'native.ssm.configure', 'native.ssm.request',
  'native.ssm.cancel', 'native.ssm.pump', 'native.ssm.forget',
  'native.ssm.sem-read', 'native.ssm.end',
  'native.source.scope-begin', 'native.source.scope-end', 'native.source.copy',
  'native.source.take-name', 'native.source.external-set',
  'native.source.external-clear', 'native.source.region-register',
  'native.source.region-unregister', 'native.source.request',
  'native.source.cancel', 'native.source.supply', 'native.source.pump',
  'native.unknown-devcom-request',
]);

const knownNativeOperations = new Set(FIRST_PAIR_NATIVE_FILE_OPERATIONS);
const sameBytes = (left, right) => left.byteLength === right.byteLength &&
  left.every((byte, index) => byte === right[index]);

function freezePlan(plan) {
  const calls = plan.audioCalls.map(call => Object.freeze({...call}));
  const frozen = {...plan, audioCalls: Object.freeze(calls)};
  for (const key of ['postClockState', 'sampledClockState', 'sampledInput']) {
    if (plan[key] && typeof plan[key] === 'object') frozen[key] = Object.freeze({...plan[key]});
  }
  return Object.freeze(frozen);
}

function validReturnedPlan(plan) {
  return !!plan && plan.returned === true && validAudioSchedule(plan) &&
    !!plan.postClockState && typeof plan.postClockState === 'object';
}

function validAudioSchedule(plan) {
  if (!plan || !Number.isSafeInteger(plan.callbackId) ||
      plan.callbackId <= 0 || !Number.isSafeInteger(plan.plannedSteps) ||
      plan.plannedSteps < 0 || !Number.isSafeInteger(plan.plannedAudioSteps) ||
      plan.plannedAudioSteps < 0 || !Number.isInteger(plan.audioPhaseBefore) ||
      plan.audioPhaseBefore < 0 || plan.audioPhaseBefore >= 60 ||
      !Number.isInteger(plan.audioPhaseAfter) || plan.audioPhaseAfter < 0 ||
      plan.audioPhaseAfter >= 60 || !Array.isArray(plan.audioCalls)) return false;
  let phase = plan.audioPhaseBefore;
  for (let index = 0; index < plan.audioCalls.length; index++) {
    const call = plan.audioCalls[index];
    if (!call || call.order !== index || call.phaseBefore !== phase ||
        !Number.isSafeInteger(call.frames) || call.frames < 0) return false;
    const total = phase + 32000;
    if (call.frames !== Math.floor(total / 60)) return false;
    phase = total % 60;
    if (call.phaseAfter !== phase) return false;
  }
  return phase === plan.audioPhaseAfter;
}

export function createFirstPairAudioFileGuard({
  getHostState,
  getNativeOwnersQuiescent,
  getCallbackIdentities,
  getCoordinatorPreLegReceipt = null,
  onFailure = () => {},
} = {}) {
  if (typeof getHostState !== 'function' ||
      typeof getNativeOwnersQuiescent !== 'function' ||
      typeof getCallbackIdentities !== 'function') {
    throw Error('First-pair guard requires concrete host, native-owner and callback readers.');
  }
  let generation = 0n;
  let unresolvedReads = 0;
  let unknownNativeOperation = null;
  let pair = null;
  let pendingPlan = null;
  let lastReturnedPlan = null;
  let lastCallbackId = 0;
  let failureNotified = false;
  let preflightRefusal = null;
  let consumedCoordinatorReceipt = null;

  const fail = (reason, detail = '') => {
    const message = detail ? `${reason}: ${detail}` : reason;
    if (pair && !pair.failure) pair.failure = message;
    if (!failureNotified) {
      failureNotified = true;
      try { onFailure(Error(message)); } catch {}
    }
    return false;
  };
  const nextGeneration = operation => {
    if (generation >= BigInt(Number.MAX_SAFE_INTEGER)) {
      fail('First-pair host read generation exhausted', operation);
      return false;
    }
    generation++;
    if (pair) return fail('Unexpected host file operation during first-pair legs', operation);
    return true;
  };
  const stateSnapshot = () => {
    const state = getHostState();
    if (!state || typeof state !== 'object') throw Error('Host file-read owner state is unavailable.');
    return {
      discSession: state.discSession,
      discIdentity: state.discIdentity,
      audioOwner: state.audioOwner,
      readResults: state.readResults,
      assetTransfer: state.assetTransfer,
      preparing: state.preparing,
      ready: state.ready,
      fatal: state.fatal,
      destroyed: state.destroyed,
      generation: generation.toString(),
      unresolvedReads,
    };
  };
  const identitiesMatch = expected => {
    const current = getCallbackIdentities();
    return !!current && expected && Object.keys(expected).every(name => current[name] === expected[name]);
  };
  const hostMatches = expected => {
    const current = stateSnapshot();
    return current.discSession === expected.discSession &&
      Object.is(current.discIdentity, expected.discIdentity) &&
      current.audioOwner === expected.audioOwner &&
      current.generation === expected.generation &&
      current.unresolvedReads === 0 && current.readResults === 0 &&
      current.assetTransfer === null && !current.preparing && current.ready &&
      !current.fatal && !current.destroyed;
  };
  const verifyPair = () => {
    if (!pair || pair.failure) return fail('First-pair guard is poisoned', pair?.failure || 'no active pair');
    if (!getNativeOwnersQuiescent()) return fail('Native file/audio owner changed or is not quiescent');
    if (!hostMatches(pair.host)) return fail('Host disc/read owner changed during first-pair legs');
    if (!identitiesMatch(pair.callbackIdentities)) return fail('Host callback identity changed during first-pair legs');
    return true;
  };

  return Object.freeze({
    beginSourceRead(operation = 'source-read-start') {
      if (typeof operation !== 'string' || !operation || !nextGeneration(operation)) return null;
      unresolvedReads++;
      return {settled: false, finished: false};
    },
    settleSourceRead(token) {
      if (!token || token.finished || token.settled) return fail('Source-read Promise settled more than once');
      token.settled = true;
      return nextGeneration('source-read-settle');
    },
    finishSourceRead(token) {
      if (!token || token.finished) return fail('Source-read Promise completion counted more than once');
      token.finished = true;
      if (!token.settled) {
        token.settled = true;
        nextGeneration('source-read-settle-without-handler');
      }
      if (unresolvedReads <= 0) return fail('Source-read Promise count underflow');
      unresolvedReads--;
      return true;
    },
    beforeSourceReadMutation(operation) {
      return nextGeneration(`source-read-${operation}`);
    },
    nativeFileOperation(operation) {
      if (typeof operation !== 'string' || !knownNativeOperations.has(operation)) {
        unknownNativeOperation = String(operation);
        return pair ? fail('Unknown native file-operation path', unknownNativeOperation) : true;
      }
      if (pair) {
        nextGeneration(operation);
        return false;
      }
      return true;
    },
    nativeFileOperationFailure(detail) {
      return fail('Native file-operation guard callback failed', String(detail));
    },
    nativeAudioTick(callbackId, order, phaseBefore, frames, phaseAfter) {
      if (!Number.isSafeInteger(callbackId) || callbackId <= 0 ||
          !Number.isSafeInteger(order) || order < 0 || !Number.isInteger(phaseBefore) ||
          !Number.isSafeInteger(frames) || frames < 0 || !Number.isInteger(phaseAfter)) {
        return fail('Native audio callback plan has malformed timing fields');
      }
      if (!pendingPlan || pendingPlan.callbackId !== callbackId || pendingPlan.returning) {
        pendingPlan = {callbackId, audioCalls: [], returning: false};
      }
      if (pair?.leg) {
        const expected = pair.plan.audioCalls[pair.leg.audioCallIndex];
        if (!expected || expected.order !== order || expected.phaseBefore !== phaseBefore ||
            expected.frames !== frames || expected.phaseAfter !== phaseAfter)
          return fail(`Native audio schedule changed at callback ${order}`);
        pair.leg.audioCallIndex++;
      }
      pendingPlan.audioCalls.push({order, phaseBefore, frames, phaseAfter});
      return true;
    },
    nativeCallbackReturning(data) {
      if (!data || !Number.isSafeInteger(data.callbackId) || data.callbackId <= lastCallbackId ||
          !Number.isSafeInteger(data.plannedSteps) || !Number.isSafeInteger(data.plannedAudioSteps) ||
          !Number.isSafeInteger(data.audioCallCount) || data.audioCallCount < 0 || !data.postClockState) {
        return fail('Native returned callback plan is incomplete or out of order');
      }
      if (!pendingPlan && data.audioCallCount === 0)
        pendingPlan = {callbackId: data.callbackId, audioCalls: [], returning: false};
      if (!pendingPlan || pendingPlan.callbackId !== data.callbackId ||
          pendingPlan.audioCalls.length !== data.audioCallCount)
        return fail('Native callback plan does not match its actual PCM schedule');
      const candidate = freezePlan({...data, returned: false, audioCalls: pendingPlan.audioCalls});
      if (!validAudioSchedule(candidate))
        return fail('Native returned callback audio schedule is malformed');
      pendingPlan.returning = true;
      queueMicrotask(() => {
        if (pendingPlan?.callbackId !== candidate.callbackId || !pendingPlan.returning) return;
        lastCallbackId = candidate.callbackId;
        lastReturnedPlan = freezePlan({...candidate, returned: true});
        pendingPlan = null;
      });
      return true;
    },
    getLastReturnedPlan() { return lastReturnedPlan; },
    hostReadSnapshot() {
      const state = stateSnapshot();
      return Object.freeze({generation: state.generation, unresolvedReads,
        resultCount: state.readResults, discSession: !!state.discSession,
        assetTransfer: state.assetTransfer !== null, preparing: !!state.preparing});
    },
    armPreLegPlan(plan, receipt) {
      const refuse = message => { preflightRefusal = message; return false; };
      if (pair) return refuse('A first-pair diagnostic is already armed');
      if (unknownNativeOperation) return refuse(`Unknown native file path was observed: ${unknownNativeOperation}`);
      if (typeof getCoordinatorPreLegReceipt !== 'function')
        return refuse('Coordinator pre-leg capability is not implemented');
      let expectedReceipt;
      try { expectedReceipt = getCoordinatorPreLegReceipt(); }
      catch { return refuse('Coordinator pre-leg capability is unavailable'); }
      if (!expectedReceipt || receipt !== expectedReceipt || receipt === consumedCoordinatorReceipt ||
          receipt.plan !== plan || receipt.callbackId !== plan?.callbackId ||
          receipt.kind !== 'melee-web-first-pair-pre-leg-v1')
        return refuse('Coordinator pre-leg receipt does not bind this exact callback plan');
      if (!plan || plan.preSideEffects !== true || !validAudioSchedule(plan) ||
          !plan.sampledInput || typeof plan.sampledInput !== 'object' ||
          !plan.sampledClockState || typeof plan.sampledClockState !== 'object')
        return refuse('Coordinator did not provide a complete sampled pre-leg plan');
      if (!getNativeOwnersQuiescent()) return refuse('Native HPS/SSM/source owners are absent, changed, or busy');
      const host = stateSnapshot();
      if (!host.discSession || host.discIdentity === null || host.discIdentity === undefined ||
          !host.audioOwner || host.readResults !== 0 || unresolvedReads !== 0 ||
          host.assetTransfer !== null || host.preparing || !host.ready || host.fatal || host.destroyed) {
        return refuse('Host disc/read/preparation owners are not quiescent');
      }
      const callbackIdentities = getCallbackIdentities();
      if (!callbackIdentities || Object.values(callbackIdentities).some(value => typeof value !== 'function'))
        return refuse('A required host callback identity is unavailable');
      const frozenPlan = freezePlan(plan);
      consumedCoordinatorReceipt = receipt;
      pair = {plan: frozenPlan, host: stateSnapshot(), callbackIdentities, leg: null,
        baseline: null, journals: {baseline: null, repeat: null}, failure: null};
      failureNotified = false;
      preflightRefusal = null;
      return Object.freeze({armed: true, callbackId: frozenPlan.callbackId,
        hostReadGeneration: pair.host.generation});
    },
    beginLeg(name, plan = null) {
      if (!verifyPair()) return false;
      if (plan === null) plan = pair.plan;
      if (plan !== pair.plan || !['baseline', 'repeat'].includes(name) || pair.leg ||
          (name === 'baseline' && pair.baseline) || (name === 'repeat' && !pair.baseline)) {
        return fail('First-pair leg order or frozen callback plan changed');
      }
      pendingPlan = null;
      pair.leg = {name, records: [], audioCallIndex: 0};
      return true;
    },
    capturePcm(pcm) {
      if (!pair) return null;
      if (!verifyPair()) return false;
      const leg = pair.leg;
      if (!leg || !(pcm instanceof Float32Array) || pcm.length % 2 !== 0) {
        return fail('PCM arrived outside a leg or with an unsupported stereo view');
      }
      const index = leg.records.length;
      const expected = pair.plan.audioCalls[index];
      if (!expected || pcm.length / 2 !== expected.frames) return fail('PCM callback count or frame length changed');
      const bytes = new Uint8Array(pcm.buffer, pcm.byteOffset, pcm.byteLength).slice();
      const record = Object.freeze({order: index, frames: pcm.length / 2, bytes});
      if (leg.name === 'repeat') {
        const baseline = pair.baseline[index];
        if (!baseline || baseline.frames !== record.frames || !sameBytes(baseline.bytes, bytes))
          return fail(`PCM differs at callback ${index}`);
      }
      leg.records.push(record);
      return true;
    },
    endLeg(name) {
      if (!verifyPair()) return false;
      if (!pair.leg || pair.leg.name !== name) return fail('First-pair leg end did not match its active leg');
      const records = pair.leg.records;
      if (records.length !== pair.plan.audioCalls.length ||
          pair.leg.audioCallIndex !== pair.plan.audioCalls.length)
        return fail('Audio or PCM callback count ended before the frozen plan');
      if (name === 'baseline') pair.baseline = records.slice();
      else if (records.some((record, index) => {
        const expected = pair.baseline[index];
        return !expected || expected.frames !== record.frames || !sameBytes(expected.bytes, record.bytes);
      })) return fail('Repeat-leg PCM journal differs from baseline');
      pair.journals[name] = Object.freeze(records.slice());
      pair.leg = null;
      return true;
    },
    snapshot() {
      return Object.freeze({generation: generation.toString(), unresolvedReads,
        unknownNativeOperation, armed: !!pair, poisoned: !!pair?.failure,
        failure: pair?.failure || null, preflightRefusal,
        activeLeg: pair?.leg?.name || null,
        callbackId: pair?.plan?.callbackId ?? null,
        pcmCounts: Object.freeze({
          baseline: pair?.journals.baseline?.length ?? null,
          repeat: pair?.journals.repeat?.length ?? null,
        })});
    },
  });
}
