// Synthetic factory controls only. This injects a test receipt into the guard;
// actual runtime owner tests separately prove that no coordinator can arm.
import assert from 'node:assert/strict';
import {createFirstPairAudioFileGuard} from '../web/first-pair-audio-file-guard.mjs';

const callbackIdentities = Object.fromEntries([
  'menuAudio', 'menuStartSourceRead', 'menuSourceReadTake', 'menuSourceReadDiscard',
  'menuFirstPairNativeFileOperation', 'menuFirstPairNativeFileOperationFailure',
  'menuFirstPairNativeAudioTick', 'menuFirstPairNativeCallbackReturning',
].map(name => [name, () => {}]));
const host = {
  discSession: {}, discIdentity: 'fixture-disc-identity', audioOwner: {},
  readResults: 0, assetTransfer: null, preparing: false,
  ready: true, fatal: false, destroyed: false,
};
let ownerQuiescent = true;
const failures = [];
function makeGuard(options = {}) {
  return createFirstPairAudioFileGuard({
    getHostState: () => host,
    getNativeOwnersQuiescent: () => ownerQuiescent,
    getCallbackIdentities: () => callbackIdentities,
    onFailure: error => failures.push(error.message),
    ...options,
  });
}

const scheduledCalls = [
  {order: 0, phaseBefore: 0, frames: 533, phaseAfter: 20},
  {order: 1, phaseBefore: 20, frames: 533, phaseAfter: 40},
];
const plan = Object.freeze({
  preSideEffects: true,
  callbackId: 73,
  plannedSteps: 3,
  plannedAudioSteps: 7,
  audioPhaseBefore: 0,
  audioPhaseAfter: 40,
  audioCalls: Object.freeze(scheduledCalls.map(Object.freeze)),
  sampledInput: Object.freeze({port0: 'held-A', pollSerial: 901}),
  sampledClockState: Object.freeze({sourceDue: 3, audioDue: 7, audioPhase: 0}),
});

// A completed ordinary callback is provenance only. It must not admit a pair.
const ordinary = makeGuard();
assert.equal(ordinary.nativeAudioTick(72, 0, 0, 533, 20), true);
assert.equal(ordinary.nativeCallbackReturning({
  callbackId: 72, plannedSteps: 1, plannedAudioSteps: 1,
  audioPhaseBefore: 0, audioPhaseAfter: 20, audioCallCount: 1,
  postClockState: {sourceFrame: 10, audioPhase: 20},
}), true);
await Promise.resolve();
assert.equal(ordinary.getLastReturnedPlan().returned, true);
assert.equal(ordinary.armPreLegPlan(plan, null), false,
  'postcallback observer and a caller-provided plan cannot arm without an injected coordinator capability');
assert.match(ordinary.snapshot().preflightRefusal, /Coordinator pre-leg capability is not implemented/);
assert.equal(ordinary.beginLeg('baseline'), false);
assert.equal(ordinary.snapshot().armed, false);

// Promise accounting survives result discard and the stale completion path.
const reads = makeGuard();
host.readResults = 1;
const staleRead = reads.beginSourceRead('source-read-start');
assert.ok(staleRead);
host.readResults = 0; // menuSourceReadDiscard removed the Map entry.
const discarded = reads.hostReadSnapshot();
assert.equal(discarded.resultCount, 0);
assert.equal(discarded.unresolvedReads, 1,
  'discard does not erase the unresolved host Promise owner');
const beforeSettlement = BigInt(discarded.generation);
assert.equal(reads.settleSourceRead(staleRead), true);
assert.equal(reads.finishSourceRead(staleRead), true);
const settled = reads.hostReadSnapshot();
assert.equal(settled.unresolvedReads, 0);
assert.ok(BigInt(settled.generation) > beforeSettlement,
  'stale Promise settlement advances the host generation before finally retires it');
assert.equal(reads.finishSourceRead(staleRead), false,
  'the finally accounting is exactly once');

// Positive pair controls use an injected, identity-bound synthetic receipt.
// Runtime construction intentionally supplies no such coordinator dependency.
const receipt = Object.freeze({
  kind: 'melee-web-first-pair-pre-leg-v1', plan, callbackId: plan.callbackId,
});
let liveReceipt = receipt;
const pairGuard = makeGuard({getCoordinatorPreLegReceipt: () => liveReceipt});
const forged = Object.freeze({...receipt});
assert.equal(pairGuard.armPreLegPlan(plan, forged), false,
  'structurally similar caller data is not the private coordinator receipt');
assert.equal(pairGuard.armPreLegPlan(plan, receipt).armed, true);
assert.equal(pairGuard.beginLeg('baseline'), true);
let workletWrites = 0;
function menuAudio(pcm) {
  const captured = pairGuard.capturePcm(pcm) ?? null;
  if (captured === true) return true;
  if (captured === false) return false;
  workletWrites++;
  return true;
}
function pcm(frames) {
  const samples = new Float32Array(frames * 2);
  for (let i = 0; i < samples.length; i++) samples[i] = ((i % 13) - 6) / 16;
  return samples;
}
for (const call of plan.audioCalls) {
  assert.equal(pairGuard.nativeAudioTick(plan.callbackId, call.order,
    call.phaseBefore, call.frames, call.phaseAfter), true);
  assert.equal(menuAudio(pcm(call.frames)), true);
}
assert.equal(pairGuard.endLeg('baseline'), true);
assert.equal(pairGuard.beginLeg('repeat'), true);
for (const call of plan.audioCalls) {
  assert.equal(pairGuard.nativeAudioTick(plan.callbackId, call.order,
    call.phaseBefore, call.frames, call.phaseAfter), true);
  assert.equal(menuAudio(pcm(call.frames)), true);
}
assert.equal(pairGuard.endLeg('repeat'), true);
assert.equal(workletWrites, 0,
  'neither speculative leg is delivered to the Worklet');
assert.deepEqual(pairGuard.snapshot().pcmCounts, {baseline: 2, repeat: 2});
assert.equal(pairGuard.snapshot().poisoned, false);

// An enqueue+complete window is caught at request admission even if queues are
// currently empty; failure is latched before the source operation can proceed.
liveReceipt = Object.freeze({kind: 'melee-web-first-pair-pre-leg-v1', plan, callbackId: plan.callbackId});
const noNewRead = makeGuard({getCoordinatorPreLegReceipt: () => liveReceipt});
assert.equal(noNewRead.armPreLegPlan(plan, liveReceipt).armed, true);
const generationBefore = BigInt(noNewRead.hostReadSnapshot().generation);
assert.equal(noNewRead.beginSourceRead('source-read-start'), null);
assert.ok(BigInt(noNewRead.hostReadSnapshot().generation) > generationBefore);
assert.equal(noNewRead.snapshot().poisoned, true);
assert.equal(noNewRead.hostReadSnapshot().unresolvedReads, 0,
  'no-new-read refusal happens before creating a Promise owner');

const residentMutation = makeGuard({getCoordinatorPreLegReceipt: () => receipt});
assert.equal(residentMutation.armPreLegPlan(plan, receipt).armed, true);
assert.equal(residentMutation.nativeFileOperation('native.ssm.request'), false);
assert.equal(residentMutation.snapshot().poisoned, true,
  'a resident request is refused even if its queue would drain before endpoint inspection');

console.log('Focused first-pair controls passed: postcallback provenance cannot arm, discarded unresolved Promise remains counted, synthetic private receipt bounds the exact scheduled PCM journal, no speculative Worklet delivery, and new host/native operations poison before side effects.');

const zero = createFirstPairAudioFileGuard({getHostState: () => ({}),
  getNativeOwnersQuiescent: () => true, getCallbackIdentities: () => ({})});
assert.equal(zero.nativeCallbackReturning({callbackId: 1, plannedSteps: 0, plannedAudioSteps: 0,
  audioCallCount: 0, audioPhaseBefore: 20, audioPhaseAfter: 20, postClockState: {audioPhase: 20}}), true);
await Promise.resolve();
assert.equal(zero.getLastReturnedPlan().audioCalls.length, 0);
assert.equal(zero.getLastReturnedPlan().returned, true);
assert.equal(zero.nativeAudioTick(2, 0, 20, 533, 40), true);
assert.equal(zero.nativeCallbackReturning({callbackId: 2, plannedSteps: 1, plannedAudioSteps: 1,
  audioCallCount: 1, audioPhaseBefore: 20, audioPhaseAfter: 40, postClockState: {audioPhase: 40}}), true);
await Promise.resolve();
assert.equal(zero.getLastReturnedPlan().audioCalls.length, 1);
console.log('Zero-audio returned provenance and later actual audio schedule PASS');
