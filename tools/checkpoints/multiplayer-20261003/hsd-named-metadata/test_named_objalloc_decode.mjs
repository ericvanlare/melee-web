#!/usr/bin/env node
import assert from 'node:assert/strict';
import {decodeNamedObjAllocCapture, SUMMARY_BYTES, ROW_BYTES,
        EXPECTED_COUNT, NULL_OFFSET, UNUSED_BYTE, NAMES} from './named_objalloc_decode.mjs';

function valid() {
  const capacity = 20;
  const summary = new Uint8Array(SUMMARY_BYTES);
  const s = new DataView(summary.buffer);
  s.setUint32(0, 1, true); s.setUint32(4, SUMMARY_BYTES, true);
  s.setUint32(8, EXPECTED_COUNT, true); s.setUint32(12, 15, true);
  s.setUint32(16, EXPECTED_COUNT, true); s.setUint32(20, 0, true);
  s.setUint32(24, 1, true); s.setUint32(28, 0, true);
  s.setUint32(32, NULL_OFFSET, true); s.setUint32(36, 0, true);
  const rows = new Uint8Array(ROW_BYTES * capacity).fill(UNUSED_BYTE);
  const view = new DataView(rows.buffer);
  for (let i = 0; i < EXPECTED_COUNT; i += 1) {
    const base = i * ROW_BYTES;
    view.setUint32(base, 1, true); view.setUint32(base + 4, i + 1, true);
    view.setUint32(base + 8, 0x1000 + i * 44, true);
    view.setUint32(base + 12, i, true);
    rows.set(new TextEncoder().encode(`${NAMES[i]}\0`), base + 16);
  }
  const base = EXPECTED_COUNT * ROW_BYTES;
  view.setUint32(base, 3, true); view.setUint32(base + 4, 0, true);
  view.setUint32(base + 8, 0x3000, true);
  view.setUint32(base + 12, 14, true);
  rows.set(new TextEncoder().encode('unmatched\0'), base + 16);
  return {summary, rows, capacity};
}

function decode(input) {
  return decodeNamedObjAllocCapture({
    summaryBytes: input.summary, rowsBytes: input.rows,
    rowCapacity: input.capacity, memoryBytes: 0x4000,
    expected: {linearBegin: 0, linearEnd: 0x4000},
  });
}

function refusal(mutator, message) {
  const input = valid(); mutator(input);
  assert.throws(() => decode(input), new RegExp(message));
}

const result = decode(valid());
assert.equal(result.summary.matched_count, 14);
assert.equal(result.summary.registry_unmatched_count, 1);
assert.equal(result.runtime_unmatched.length, 1);
refusal(input => new DataView(input.summary.buffer).setUint32(20, 1, true), 'counts');
refusal(input => new DataView(input.summary.buffer).setUint32(28, 1, true), 'complete');
refusal(input => new DataView(input.rows.buffer).setUint32(4, 0, true), 'name id');
refusal(input => input.rows.fill(0, ROW_BYTES * 15), 'unused named-row tail');
refusal(input => new DataView(input.rows.buffer).setUint32(14 * ROW_BYTES + 8, 0x1000, true), 'duplicate.*offset');
refusal(input => new DataView(input.rows.buffer).setUint32(14 * ROW_BYTES + 12, 0, true), 'duplicate.*ordinal');
refusal(input => {
  const view = new DataView(input.rows.buffer);
  view.setUint32(0, 2, true);
  view.setUint32(8, NULL_OFFSET, true);
  view.setUint32(12, NULL_OFFSET, true);
}, 'status counts');
// Claiming one registry entry that has no emitted row must fail before the
// summary can make an incomplete row set look complete.
refusal(input => new DataView(input.summary.buffer).setUint32(12, 16, true), 'counts');
console.log(JSON.stringify({result: 'passed', controls: 9,
                            scope: 'synthetic named ABI; no runtime'}));
