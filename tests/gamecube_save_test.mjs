import assert from 'node:assert/strict';
import {createMeleeGCI, parseMeleeGCI, MELEE_GCI_FILE_BYTES,
  MELEE_GCI_PROFILE_BYTES} from '../web/gamecube-save.mjs';

const HEADER = 0x40;
const BLOCK = 0x2000;
const KEY = [0x26, 0xff, 0xe8, 0xef, 0x42, 0xd6, 0x01, 0x54, 0x14, 0xa3, 0x80, 0xfd, 0x6e];
const PERMUTATION = [
  [0, 4, 1, 5, 2, 6, 3, 7], [3, 0, 2, 6, 5, 4, 7, 1],
  [6, 5, 0, 1, 3, 7, 2, 4], [1, 3, 7, 4, 0, 2, 5, 6],
  [7, 2, 5, 0, 6, 1, 4, 3], [5, 6, 4, 3, 7, 0, 1, 2],
  [2, 1, 6, 7, 4, 3, 0, 5],
];

function referenceDigest(bytes) {
  const state = Uint8Array.from([1, 35, 69, 103, 137, 171, 205, 239, 254, 220, 186, 152, 118, 84, 50, 16]);
  for (let i = 0; i < bytes.length; i++) state[i % 16] = (state[i % 16] + bytes[i]) & 0xff;
  for (let i = 1; i < 16; i++) if (state[i - 1] === state[i]) state[i] ^= 0xff;
  return state;
}

function changeRecord(record, {sequence, payloadOffset, payloadValue}) {
  const decoded = new Uint8Array(record);
  let previous = record[15];
  for (let i = 16; i < record.length; i++) {
    const encoded = record[i];
    const permutation = PERMUTATION[previous % 7];
    let bits = 0;
    for (let source = 0; source < 8; source++) bits |= ((encoded >> permutation[source]) & 1) << source;
    decoded[i] = bits ^ previous ^ KEY[previous % 13];
    previous = encoded;
  }
  if (sequence !== undefined) decoded[0x12] = sequence;
  if (payloadOffset !== undefined) decoded[0x20 + payloadOffset] = payloadValue;
  decoded.set(referenceDigest(decoded.subarray(16)), 0);
  for (let i = 16; i < decoded.length; i++) {
    const previousCipher = decoded[i - 1];
    const permutation = PERMUTATION[previousCipher % 7];
    const transformed = decoded[i] ^ previousCipher ^ KEY[previousCipher % 13];
    let encoded = 0;
    for (let bit = 0; bit < 8; bit++) encoded |= ((transformed >> bit) & 1) << permutation[bit];
    decoded[i] = encoded;
  }
  return decoded;
}

function recordAt(gci, physical) {
  return gci.slice(HEADER + physical * BLOCK, HEADER + (physical + 1) * BLOCK);
}

function setRecord(gci, physical, record) {
  gci.set(record, HEADER + physical * BLOCK);
}

const profile = Uint8Array.from({length: MELEE_GCI_PROFILE_BYTES}, (_, index) => index * 29 & 0xff);
profile[0] = 0x07; profile[1] = 0xff;
profile[2] = 0x01; profile[3] = 0xc0;
const at = new Date('2026-09-26T20:10:11.000Z');
const {bytes, fileName} = createMeleeGCI(profile, at);
assert.equal(MELEE_GCI_FILE_BYTES, 90_176);
assert.equal(bytes.length, MELEE_GCI_FILE_BYTES);
assert.equal(fileName, 'melee-save-GALE01-20260926T201011Z.gci');
assert.deepEqual(parseMeleeGCI(bytes), profile);

for (const mutate of [
  copy => { copy[0] = 0x45; },
  copy => { copy[0x39] = 0; },
  copy => { copy[0x40 + 2 * 0x2000 + 0x100] ^= 0x80; },
]) {
  const damaged = new Uint8Array(bytes);
  mutate(damaged);
  assert.throws(() => parseMeleeGCI(damaged));
}

const moved = new Uint8Array(bytes);
const bankRecord = recordAt(moved, 2);
const spareRecord = recordAt(moved, 9);
setRecord(moved, 2, spareRecord);
setRecord(moved, 9, bankRecord);
assert.deepEqual(parseMeleeGCI(moved), profile, 'logical bank identities may move between physical blocks');

const changedProfile = new Uint8Array(profile);
changedProfile[0] ^= 0x5a;
const changedGci = createMeleeGCI(changedProfile, at).bytes;
const wrapped = new Uint8Array(bytes);
setRecord(wrapped, 1, changeRecord(recordAt(bytes, 1), {sequence: 0xff}));
setRecord(wrapped, 10, recordAt(changedGci, 1));
assert.deepEqual(parseMeleeGCI(wrapped), changedProfile,
  'the newer redundant SaveData record at sequence 0 wins over sequence 0xff after wrap');

const interrupted = new Uint8Array(wrapped);
interrupted[0x40 + 10 * 0x2000 + 0x100] ^= 0x80;
assert.deepEqual(parseMeleeGCI(interrupted), profile,
  'an unauthenticated redundant SaveData block is ignored while the valid copy remains readable');

const bothSaveDataCopiesCorrupt = new Uint8Array(bytes);
bothSaveDataCopiesCorrupt[0x40 + 1 * 0x2000 + 0x100] ^= 0x80;
bothSaveDataCopiesCorrupt[0x40 + 10 * 0x2000 + 0x100] ^= 0x80;
assert.throws(() => parseMeleeGCI(bothSaveDataCopiesCorrupt), /missing Melee data record 1/,
  'a corrupt redundant record is recoverable only while another authenticated copy remains');

const changedSaveData = new Uint8Array(profile);
changedSaveData[0] ^= 0x31;
const conflictingCopies = new Uint8Array(bytes);
setRecord(conflictingCopies, 10, recordAt(createMeleeGCI(changedSaveData, at).bytes, 1));
assert.throws(() => parseMeleeGCI(conflictingCopies), /conflicting current copies/,
  'same-sequence redundant SaveData copies must agree');

console.log('Melee GCI extent, logical relocation, sequence wrap, redundancy, and corruption recovery passed');
