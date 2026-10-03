// Synthetic controls for the diagnostic shared-page snapshot owner. This
// target uses only Uint8Array memory and stack stubs; it makes no browser,
// renderer, audio, source-runtime, or rollback-admission claim.
import assert from 'node:assert/strict';
import { SharedPageWasmSnapshotRing } from './shared_page_wasm_snapshot.mjs';

const pageBytes = 65536;
const makeModule = () => ({
  HEAPU8: new Uint8Array(pageBytes * 2),
  stackSave: () => 12,
  stackRestore: value => assert.equal(value, 12),
});

// The first capture hashes every page. A changed page misses the most-recent
// hint and uses the SHA-256/exact-collision path, while the unchanged page
// retains the hinted entry. Releasing the newest snapshot leaves the earlier
// snapshot as the hint; releasing every snapshot removes both page ownership
// and diagnostic capture metadata.
const module = makeModule();
const owner = new SharedPageWasmSnapshotRing(module, {
  maxBytes: pageBytes * 2,
  maxRetainedPageBytes: pageBytes * 2,
});
const first = owner.capture();
assert.deepEqual(owner.stats().lastCapture, {
  hintedSnapshot: false, pageCount: 2, hintComparisons: 0, hintHits: 0, hashedPages: 2,
});
module.HEAPU8[pageBytes] = 7;
const second = owner.capture();
assert.deepEqual(owner.stats().lastCapture, {
  hintedSnapshot: true, pageCount: 2, hintComparisons: 2, hintHits: 1, hashedPages: 1,
});
second.release();
assert.equal(owner.stats().retainedSnapshots, 1);
module.HEAPU8.fill(0);
const third = owner.capture();
assert.deepEqual(owner.stats().lastCapture, {
  hintedSnapshot: true, pageCount: 2, hintComparisons: 2, hintHits: 2, hashedPages: 0,
});
third.release();
first.release();
assert.equal(owner.stats().retainedSnapshots, 0);
assert.equal(owner.stats().uniquePageBytes, 0);
assert.equal(owner.stats().lastCapture, null);
module.HEAPU8[0] = 3;
const afterAllRelease = owner.capture();
assert.deepEqual(owner.stats().lastCapture, {
  hintedSnapshot: false, pageCount: 2, hintComparisons: 0, hintHits: 0, hashedPages: 2,
});
afterAllRelease.release();
assert.equal(owner.stats().uniquePageBytes, 0);
assert.equal(owner.stats().lastCapture, null);
owner.close();

// A changed page must fail the hard unique-page budget, and partial references
// from the failed capture must roll back. After the only record is released,
// the next capture has no hint and performs first-capture hashing again.
const budgetModule = makeModule();
const budgetOwner = new SharedPageWasmSnapshotRing(budgetModule, {
  maxBytes: pageBytes * 2,
  maxRetainedPageBytes: pageBytes,
});
const budgetFirst = budgetOwner.capture();
budgetModule.HEAPU8[pageBytes] = 9;
assert.throws(() => budgetOwner.capture(), /retained payload exceeds/);
assert.equal(budgetOwner.stats().retainedSnapshots, 1);
assert.equal(budgetOwner.stats().uniquePageBytes, pageBytes);
budgetFirst.release();
assert.equal(budgetOwner.stats().retainedSnapshots, 0);
assert.equal(budgetOwner.stats().uniquePageBytes, 0);
assert.equal(budgetOwner.stats().lastCapture, null);
budgetModule.HEAPU8.fill(0);
const budgetAgain = budgetOwner.capture();
assert.deepEqual(budgetOwner.stats().lastCapture, {
  hintedSnapshot: false, pageCount: 2, hintComparisons: 0, hintHits: 0, hashedPages: 2,
});
budgetAgain.release();
assert.equal(budgetOwner.stats().uniquePageBytes, 0);
assert.equal(budgetOwner.stats().lastCapture, null);
budgetOwner.close();

// Preserve the existing owner-boundary controls while exercising the new
// page-sharing implementation: stack failure rollback, active-call rejection,
// identity replacement, async poison, and owner-close invalidation.
const boundaryModule = makeModule();
const boundaryOwner = new SharedPageWasmSnapshotRing(boundaryModule, {
  maxBytes: pageBytes * 2,
  maxRetainedPageBytes: pageBytes,
});
const boundaryFirst = boundaryOwner.capture();
boundaryModule.stackSave = () => { throw new Error('test stack failure'); };
assert.throws(() => boundaryOwner.capture(), /test stack failure/);
assert.equal(boundaryOwner.stats().uniquePageBytes, pageBytes);
boundaryModule.stackSave = () => 12;
assert.throws(() => boundaryOwner.call(() => boundaryFirst.restore()), /exported-call return/);
assert.throws(() => boundaryOwner.call(() => boundaryOwner.capture()), /exported-call return/);
boundaryFirst.release();
assert.equal(boundaryOwner.stats().uniquePageBytes, 0);
boundaryOwner.close();

const replacementHeap = new Uint8Array(pageBytes * 2);
// Replacing the owner module's heap must be rejected before capture.
const replacementModule = makeModule();
const replacementOwner = new SharedPageWasmSnapshotRing(replacementModule);
replacementModule.HEAPU8 = replacementHeap;
assert.throws(() => replacementOwner.capture(), /identity, view, or capacity changed/);
replacementOwner.close();

// Eight handles share immutable page ownership. A later live-memory write
// cannot change the saved bytes, and a ninth capture is refused before any
// additional ownership is acquired.
const ringModule = makeModule();
ringModule.HEAPU8[0] = 23;
const ringOwner = new SharedPageWasmSnapshotRing(ringModule);
const ring = Array.from({ length: 8 }, () => ringOwner.capture());
assert.equal(ringOwner.stats().retainedSnapshots, 8);
const ownedAtLimit = ringOwner.stats().uniquePageBytes;
assert.throws(() => ringOwner.capture(), /snapshot limit is 8/);
assert.equal(ringOwner.stats().uniquePageBytes, ownedAtLimit);
ringModule.HEAPU8[0] = 91;
ring[0].restore();
assert.equal(ringModule.HEAPU8[0], 23);
ringModule.stackSave = () => 16;
assert.throws(() => ring[0].restore(), /stack is not at the captured boundary/);
for (const snapshot of ring) snapshot.release();
assert.equal(ringOwner.stats().uniquePageBytes, 0);
ringOwner.close();

const poisonedModule = makeModule();
const poisoned = new SharedPageWasmSnapshotRing(poisonedModule);
assert.throws(() => poisoned.call(() => Promise.resolve(1)), /poisoned/);
assert.throws(() => poisoned.capture(), /exported-call return/);
assert.throws(() => poisoned.close(), /active diagnostic call/);

const closeModule = makeModule();
const closeOwner = new SharedPageWasmSnapshotRing(closeModule);
const closeSnapshot = closeOwner.capture();
closeOwner.close();
assert.throws(() => closeSnapshot.restore(), /closed/);
assert.throws(() => closeSnapshot.release(), /already released/);

console.log('shared-page snapshot controls passed');
