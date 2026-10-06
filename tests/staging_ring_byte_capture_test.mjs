import assert from 'node:assert/strict';
import test from 'node:test';
import {armStagingRingByteCapture, compareStagingRingByteCaptures,
  installStagingRingByteCapture, readStagingRingByteCapture,
  STAGING_RING_BYTE_HASH_FRAME_CAP} from './staging_ring_byte_capture.mjs';

function ringStatus(slots) {
  return {frame_slots: slots, staging_buffers: slots,
    selection: {byte_hash_enabled: true, selected_frame_slots: slots}};
}

function capture(slot, {rendererOffset = 0, mismatchAt = null, cursorMismatchAt = null} = {}) {
  const frames = Array.from({length: STAGING_RING_BYTE_HASH_FRAME_CAP}, (_, index) => ({
    gameplay_ordinal: index + 1,
    source_cursor: index,
    source_frame: Math.max(0, index - 120),
    source_draw_ordinal: index + 1,
    renderer_frame_id: index + rendererOffset,
    renderer_frame_index: index + rendererOffset,
    staging_slot: slot,
    writes: [{destination_role: 0, destination_role_name: 'vertex', destination_offset: 0,
      byte_length: 16, sha256: mismatchAt === index ? 'f'.repeat(64) : 'a'.repeat(64)}],
  }));
  if (cursorMismatchAt !== null) frames[cursorMismatchAt].source_cursor++;
  return {schema: 'melee-web-staging-byte-hashes-v1', status: 'complete',
    frame_count: STAGING_RING_BYTE_HASH_FRAME_CAP, pending_frame_count: 0, expected_cursor: 600,
    requested_slots: slot === 0 ? 2 : 4, ring_status: ringStatus(slot === 0 ? 2 : 4),
    overflow: false, errors: [], frames};
}

function installMockCapture() {
  globalThis.window = {};
  const page = {evaluate: async (fn, arg) => fn(arg)};
  return installStagingRingByteCapture(page).then(() => ({page,
    capture: () => window.__meleeWebStagingByteCapture}));
}

function fakeFrame(sourceFrame = 0, sourceCursor = 0, slot = 0, marker = 'a') {
  return {frame_id: 100, frame_index: 99, staging_slot: slot,
    source_frame: sourceFrame, source_cursor: sourceCursor,
    writes: [{destination_role: 0, destination_offset: 0, byte_length: 16, sha256: marker.repeat(64)}]};
}

test('compares submitted ranges and source identity while allowing slot/global renderer IDs to differ', () => {
  const result = compareStagingRingByteCaptures(capture(0), capture(3, {rendererOffset: 17}));
  assert.equal(result.status, 'equal');
  assert.equal(result.frames, 600);
});

test('rejects first submitted-byte hash divergence', () => {
  assert.throws(() => compareStagingRingByteCaptures(capture(0), capture(3, {mismatchAt: 27})),
    /gameplay ordinal 28/);
});

test('rejects source-cursor divergence even when upload hashes agree', () => {
  assert.throws(() => compareStagingRingByteCaptures(capture(0), capture(3, {cursorMismatchAt: 9})),
    /missing, duplicate, or reordered source cursor at gameplay ordinal 10/);
});

test('validates each capture sequence independently, including duplicated and reordered rows', () => {
  const duplicate = capture(0);
  duplicate.frames[19].source_cursor = duplicate.frames[18].source_cursor;
  assert.throws(() => compareStagingRingByteCaptures(duplicate, capture(3)), /ring2 capture has a missing, duplicate, or reordered/);
  const reordered = capture(0);
  [reordered.frames[3], reordered.frames[4]] = [reordered.frames[4], reordered.frames[3]];
  assert.throws(() => compareStagingRingByteCaptures(reordered, capture(3)), /ring2 capture has a missing, duplicate, or reordered/);
});

test('rejects incomplete, missing, overflowing, or mislabeled ring captures', () => {
  const incomplete = capture(0);
  incomplete.status = 'incomplete';
  assert.throws(() => compareStagingRingByteCaptures(incomplete, capture(3)), /ring2 capture is incomplete/);
  const missing = capture(0);
  missing.frames.pop();
  assert.throws(() => compareStagingRingByteCaptures(missing, capture(3)), /ring2 capture is incomplete/);
  const overflow = capture(0);
  overflow.overflow = true;
  assert.throws(() => compareStagingRingByteCaptures(overflow, capture(3)), /ring2 capture is incomplete/);
  const wrongStatus = capture(3);
  wrongStatus.ring_status.frame_slots = 2;
  assert.throws(() => compareStagingRingByteCaptures(capture(0), wrongStatus), /ring4 ring identity/);
});

test('mock installer excludes preparation, enforces first cursor and records tagged source draws', async () => {
  const {page, capture: current} = await installMockCapture();
  await armStagingRingByteCapture(page);
  const installed = current();
  installed.recordFrame(fakeFrame()); // no source tag: preparation/untagged renderer work is excluded
  assert.equal(installed.frames.length, 0);
  assert.equal(installed.setSourceDrawTag(2, 1), false);
  assert.equal(installed.status, 'incomplete');
  assert.deepEqual(installed.errors, ['first_source_cursor_or_frame_mismatch']);
});

test('mock installer fails duplicate, missing, or reordered tagged draws', async () => {
  const {page, capture: current} = await installMockCapture();
  await armStagingRingByteCapture(page);
  const installed = current();
  assert.equal(installed.setSourceDrawTag(0, 0), true);
  installed.recordFrame(fakeFrame(0, 0));
  assert.equal(installed.frames.length, 1);
  assert.equal(installed.setSourceDrawTag(3, 2), false);
  assert.deepEqual(installed.errors, ['missing_duplicate_or_reordered_source_cursor']);

  const duplicateCapture = await installMockCapture();
  await armStagingRingByteCapture(duplicateCapture.page);
  assert.equal(duplicateCapture.capture().setSourceDrawTag(0, 0), true);
  duplicateCapture.capture().recordFrame(fakeFrame(0, 0));
  assert.equal(duplicateCapture.capture().setSourceDrawTag(2, 0), false);
  assert.deepEqual(duplicateCapture.capture().errors, ['missing_duplicate_or_reordered_source_cursor']);
});

test('capture stops at cursor 599, and later catch-up draws do not overflow the 600-frame cap', async () => {
  const {page, capture: current} = await installMockCapture();
  await armStagingRingByteCapture(page);
  const installed = current();
  for (let cursor = 0; cursor < 600; cursor++) {
    assert.equal(installed.setSourceDrawTag(Math.max(0, cursor - 120), cursor), true);
    installed.recordFrame(fakeFrame(Math.max(0, cursor - 120), cursor, cursor % 4));
  }
  assert.equal(installed.status, 'complete');
  assert.equal(installed.expected_cursor, 600);
  assert.equal(installed.frames.length, 600);
  assert.equal(installed.setSourceDrawTag(601, 600), false);
  installed.recordFrame(fakeFrame(601, 600));
  assert.equal(installed.status, 'complete');
  assert.equal(installed.overflow, false);
  const snapshot = await readStagingRingByteCapture(page);
  assert.equal(snapshot.frames.length, 600);
});

test('raw original match counter may hold during entry, but its origin and cross-ring identity remain checked', async () => {
  const {page, capture: current} = await installMockCapture();
  await armStagingRingByteCapture(page);
  assert.equal(current().setSourceDrawTag(1, 0), false);
  assert.deepEqual(current().rejected_source_tag,
    {source_frame: 1, source_cursor: 0, expected_cursor: 0});
  const other = capture(3);
  other.frames[150].source_frame++;
  assert.throws(() => compareStagingRingByteCaptures(capture(0), other), /mismatch at gameplay ordinal 151/);
});
