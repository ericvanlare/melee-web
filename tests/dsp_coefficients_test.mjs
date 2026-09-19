import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {AUDIO_FILTER_SHA256, createAudioFilterTable} from '../web/dsp-coefficients.mjs';

const EXPECTED_SHA256 = 'd7741279c2e8ec5c5fb318f8fbdd6de6bf583520d288e836a5383233a4238179';
const WORDS_PER_BANK = 512;

function digest(bytes) {
  return createHash('sha256').update(bytes).digest('hex');
}

function signedWord(bytes, wordIndex) {
  const byteIndex = wordIndex * 2;
  const unsigned = (bytes[byteIndex] << 8) | bytes[byteIndex + 1];
  return unsigned < 0x8000 ? unsigned : unsigned - 0x10000;
}

const table = createAudioFilterTable();
assert.equal(table.constructor, Uint8Array);
assert.equal(table.byteLength, 4096);
assert.equal(AUDIO_FILTER_SHA256, EXPECTED_SHA256);
assert.equal(digest(table), EXPECTED_SHA256);

// The table is four independent big-endian banks. The reserved bank starts
// zero; four explicit compatibility words also land in that bank.
const reservedCompatibility = new Map([
  [0x66c, 0x06f2], [0x6fe, 0x0008], [0x723, 0xffe0], [0x766, 0x0273],
]);
for (let word = WORDS_PER_BANK * 3; word < WORDS_PER_BANK * 4; word += 1) {
  const expectedUnsigned = reservedCompatibility.get(word);
  const expectedSigned = expectedUnsigned === undefined ? 0 :
    (expectedUnsigned < 0x8000 ? expectedUnsigned : expectedUnsigned - 0x10000);
  assert.equal(signedWord(table, word), expectedSigned, `reserved bank word ${word}`);
}

// Every generated phase is a bounded, positive-gain four-tap row.
for (let bank = 0; bank < 3; bank += 1) {
  for (let phase = 0; phase < 128; phase += 1) {
    let rowSum = 0;
    let rowHasSignal = false;
    for (let tap = 0; tap < 4; tap += 1) {
      const word = bank * WORDS_PER_BANK + phase * 4 + tap;
      const value = signedWord(table, word);
      assert(value >= -32768 && value <= 32767);
      rowSum += value;
      rowHasSignal ||= value !== 0;
    }
    assert(rowHasSignal, `bank ${bank} phase ${phase} is empty`);
    assert(rowSum > 0, `bank ${bank} phase ${phase} has non-positive gain`);
  }
}

// Check representative compatibility words and their big-endian encoding.
for (const [wordIndex, unsignedValue] of [
  [0x03b, 0x0065], [0x0ca, 0x3461], [0x468, 0xf808], [0x723, 0xffe0],
]) {
  const actualUnsigned = (table[wordIndex * 2] << 8) | table[wordIndex * 2 + 1];
  assert.equal(actualUnsigned, unsignedValue, `compatibility word ${wordIndex.toString(16)}`);
}

// Callers receive independent storage, including after one result is mutated.
const second = createAudioFilterTable();
assert.notStrictEqual(second, table);
assert.deepEqual(second, table);
table[0] ^= 0xff;
assert.notEqual(table[0], second[0]);
assert.equal(digest(second), EXPECTED_SHA256);

console.log(`audio filter table: ${EXPECTED_SHA256}`);
