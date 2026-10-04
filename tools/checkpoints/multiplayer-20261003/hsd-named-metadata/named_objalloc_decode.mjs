/* Decoder for the private exact-pointer/name allocator ABI.  It is diagnostic
 * only: rows are identities from the source getter table or explicit
 * runtime-unmatched entries, never inferred names. */
export const ABI_VERSION = 1;
export const SUMMARY_BYTES = 40;
export const ROW_BYTES = 32;
export const EXPECTED_COUNT = 14;
export const NULL_OFFSET = 0xffffffff;
export const UNUSED_BYTE = 0xa5;
export const NAMES = [
  'aobj', 'fobj', 'id', 'slist', 'dlist', 'vec', 'mtx', 'robj',
  'rval', 'shadow', 'render', 'chan', 'tevreg', 'zlist',
];

function u32(view, offset) { return view.getUint32(offset, true); }
function bytes(value, label) {
  if (!(value instanceof Uint8Array)) throw new Error(`${label} must be Uint8Array`);
  return value;
}
function inRange(value, begin, end, size, label) {
  if (value < begin || value > end || size > end - value)
    throw new Error(`${label} is outside linear memory`);
}
function text(bytesValue) {
  const end = bytesValue.indexOf(0);
  if (end < 0) throw new Error('named row has no NUL terminator');
  return new TextDecoder().decode(bytesValue.slice(0, end));
}

export function decodeNamedObjAllocCapture({summaryBytes, rowsBytes,
                                             rowCapacity, memoryBytes,
                                             expected = {}}) {
  const summaryRaw = bytes(summaryBytes, 'summary bytes');
  const rowsRaw = bytes(rowsBytes, 'row bytes');
  if (summaryRaw.byteLength !== SUMMARY_BYTES)
    throw new Error(`summary ABI size is ${summaryRaw.byteLength}, expected ${SUMMARY_BYTES}`);
  if (!Number.isSafeInteger(rowCapacity) || rowCapacity < EXPECTED_COUNT)
    throw new Error('named row capacity is below the 14-entry source table');
  if (rowsRaw.byteLength !== ROW_BYTES * rowCapacity)
    throw new Error('named row buffer size does not match caller capacity');
  if (!Number.isSafeInteger(memoryBytes) || memoryBytes <= 0)
    throw new Error('linear-memory size is invalid');

  const summaryView = new DataView(summaryRaw.buffer, summaryRaw.byteOffset,
                                   summaryRaw.byteLength);
  const summary = {
    abi_version: u32(summaryView, 0), abi_size: u32(summaryView, 4),
    expected_count: u32(summaryView, 8), registry_count: u32(summaryView, 12),
    matched_count: u32(summaryView, 16), expected_unmatched_count: u32(summaryView, 20),
    registry_unmatched_count: u32(summaryView, 24), complete: u32(summaryView, 28),
    null_offset: u32(summaryView, 32), reserved: u32(summaryView, 36),
  };
  if (summary.abi_version !== ABI_VERSION || summary.abi_size !== SUMMARY_BYTES)
    throw new Error('named summary ABI/version mismatch');
  if (summary.expected_count !== EXPECTED_COUNT)
    throw new Error('named summary expected count is not 14');
  if (!summary.registry_count || summary.matched_count > EXPECTED_COUNT ||
      summary.expected_unmatched_count > EXPECTED_COUNT ||
      summary.registry_unmatched_count > summary.registry_count ||
      summary.matched_count + summary.expected_unmatched_count !== EXPECTED_COUNT ||
      summary.matched_count + summary.registry_unmatched_count !== summary.registry_count)
    throw new Error('named summary counts are inconsistent');
  if (summary.null_offset !== NULL_OFFSET || summary.reserved !== 0)
    throw new Error('named summary null/reserved fields are invalid');
  if (summary.complete !== Number(summary.matched_count === EXPECTED_COUNT &&
                                  summary.registry_unmatched_count === 0))
    throw new Error('named summary complete flag is inconsistent');
  if (expected.linearBegin !== undefined && expected.linearBegin !== 0)
    throw new Error('named decoder requires linear memory to begin at zero');
  const linearEnd = expected.linearEnd ?? memoryBytes;
  if (!Number.isSafeInteger(linearEnd) || linearEnd <= 0 || linearEnd > memoryBytes)
    throw new Error('named linear-memory bound is invalid');

  const rowView = new DataView(rowsRaw.buffer, rowsRaw.byteOffset, rowsRaw.byteLength);
  const rows = [];
  const seenNames = new Set();
  const seenOrdinals = new Set();
  const seenOffsets = new Set();
  let matchedRows = 0;
  let expectedUnmatchedRows = 0;
  for (let index = 0; index < EXPECTED_COUNT; index += 1) {
    const base = index * ROW_BYTES;
    const rowBytes = rowsRaw.slice(base, base + ROW_BYTES);
    const row = {
      status: u32(rowView, base), name_id: u32(rowView, base + 4),
      descriptor_offset: u32(rowView, base + 8),
      registry_ordinal: u32(rowView, base + 12), name: text(rowBytes.slice(16, 32)),
    };
    if (row.name_id < 1 || row.name_id > EXPECTED_COUNT || seenNames.has(row.name_id))
      throw new Error('named source rows repeat or omit a name id');
    seenNames.add(row.name_id);
    if (row.name !== NAMES[row.name_id - 1])
      throw new Error('named source row label differs from authored getter table');
    if (row.status === 1) {
      inRange(row.descriptor_offset, 0, linearEnd, 44,
              `named descriptor ${row.name_id}`);
      if (row.registry_ordinal >= summary.registry_count)
        throw new Error('named matched row registry ordinal is outside registry');
      if (seenOrdinals.has(row.registry_ordinal) || seenOffsets.has(row.descriptor_offset))
        throw new Error('named matched rows duplicate a registry ordinal or descriptor offset');
      seenOrdinals.add(row.registry_ordinal);
      seenOffsets.add(row.descriptor_offset);
      matchedRows += 1;
    } else if (row.status === 2) {
      if (row.descriptor_offset !== NULL_OFFSET || row.registry_ordinal !== NULL_OFFSET)
        throw new Error('expected-unmatched row has a non-null identity');
      expectedUnmatchedRows += 1;
    } else {
      throw new Error('named source row has an unexpected status');
    }
    rows.push(row);
  }
  for (let index = EXPECTED_COUNT;
       index < EXPECTED_COUNT + summary.registry_unmatched_count; index += 1) {
    const base = index * ROW_BYTES;
    const rowBytes = rowsRaw.slice(base, base + ROW_BYTES);
    const row = {
      status: u32(rowView, base), name_id: u32(rowView, base + 4),
      descriptor_offset: u32(rowView, base + 8),
      registry_ordinal: u32(rowView, base + 12), name: text(rowBytes.slice(16, 32)),
    };
    if (row.status !== 3 || row.name_id !== 0 || row.name !== 'unmatched')
      throw new Error('runtime-unmatched row is malformed');
    inRange(row.descriptor_offset, 0, linearEnd, 44, 'runtime-unmatched descriptor');
    if (row.registry_ordinal >= summary.registry_count)
      throw new Error('runtime-unmatched row ordinal is outside registry');
    if (seenOrdinals.has(row.registry_ordinal) || seenOffsets.has(row.descriptor_offset))
      throw new Error('runtime-unmatched rows duplicate a registry ordinal or descriptor offset');
    seenOrdinals.add(row.registry_ordinal);
    seenOffsets.add(row.descriptor_offset);
    rows.push(row);
  }
  if (matchedRows !== summary.matched_count ||
      expectedUnmatchedRows !== summary.expected_unmatched_count)
    throw new Error('emitted named row status counts differ from summary');
  if (seenOrdinals.size !== summary.registry_count)
    throw new Error('emitted rows do not cover every registry ordinal');
  const tailStart = (EXPECTED_COUNT + summary.registry_unmatched_count) * ROW_BYTES;
  for (let offset = tailStart; offset < rowsRaw.byteLength; offset += 1) {
    if (rowsRaw[offset] !== UNUSED_BYTE)
      throw new Error(`unused named-row tail byte ${offset} was modified`);
  }
  return {summary, rows, runtime_unmatched: rows.filter(row => row.status === 3)};
}
