/**
 * Build the browser audio filter banks from the port's declared filter
 * specification (docs/AUDIO_FILTER_DESIGN.md). Each call returns fresh storage.
 */

const PHASE_COUNT = 128;
const TAPS_PER_PHASE = 4;
const WORDS_PER_BANK = PHASE_COUNT * TAPS_PER_PHASE;
const BANK_COUNT = 4;
const BYTES_PER_WORD = 2;
const TABLE_BYTES = BANK_COUNT * WORDS_PER_BANK * BYTES_PER_WORD;

const KAISER_BETA = 9 * Math.PI / 4;
const KAISER_QUADRATURE_STEPS = 1024;

// These are compatibility values observed in the replacement DROM table.
// They are applied as unsigned 16-bit words after numerical generation.
const COMPATIBILITY_WORDS = Object.freeze([
  [0x03b, 0x0065], [0x043, 0x0076], [0x0ca, 0x3461], [0x0e2, 0x376f],
  [0x1b8, 0x007f], [0x1f8, 0x0009], [0x1fc, 0x0003], [0x229, 0x657c],
  [0x231, 0x64fc], [0x259, 0x6143], [0x285, 0x5aff], [0x456, 0x102f],
  [0x468, 0xf808], [0x491, 0x6a0f], [0x5f1, 0x0200], [0x5f6, 0x7f65],
  [0x66c, 0x06f2], [0x6fe, 0x0008], [0x723, 0xffe0], [0x766, 0x0273],
]);

/**
 * Evaluate I_0(x) with the integral definition and composite Simpson
 * quadrature.  The arguments used by the Kaiser window are small enough for
 * direct exponentials, while the symmetric endpoint sum avoids cancellation.
 */
function modifiedBesselOrderZero(x) {
  if (x === 0) return 1;

  const step = Math.PI / KAISER_QUADRATURE_STEPS;
  let total = Math.exp(x) + Math.exp(-x);
  for (let k = 1; k < KAISER_QUADRATURE_STEPS; k += 1) {
    const sample = Math.exp(x * Math.cos(k * step));
    total += (k % 2 === 0 ? 2 : 4) * sample;
  }
  return total * step / (3 * Math.PI);
}

function normalizedSinc(value) {
  return value === 0 ? 1 : Math.sin(Math.PI * value) / (Math.PI * value);
}

function nearestEven(value) {
  const lower = Math.floor(value);
  const fraction = value - lower;
  if (fraction < 0.5) return lower;
  if (fraction > 0.5) return lower + 1;
  return lower % 2 === 0 ? lower : lower + 1;
}

function putBigEndianWord(bytes, wordIndex, signedValue) {
  const unsignedValue = signedValue < 0 ? signedValue + 0x10000 : signedValue;
  const byteIndex = wordIndex * BYTES_PER_WORD;
  bytes[byteIndex] = unsignedValue >>> 8;
  bytes[byteIndex + 1] = unsignedValue & 0xff;
}

function bankWindow(bank, gridIndex, kaiserDenominator) {
  const normalizedPosition = 2 * gridIndex / 511 - 1;
  if (bank === 1) {
    const radial = Math.sqrt(Math.max(0, 1 - normalizedPosition * normalizedPosition));
    return modifiedBesselOrderZero(KAISER_BETA * radial) / kaiserDenominator;
  }
  return 0.54 - 0.46 * Math.cos(2 * Math.PI * gridIndex / 511);
}

export const AUDIO_FILTER_SHA256 =
  'd7741279c2e8ec5c5fb318f8fbdd6de6bf583520d288e836a5383233a4238179';

export function createAudioFilterTable() {
  const table = new Uint8Array(TABLE_BYTES);

  for (let bank = 0; bank < 3; bank += 1) {
    const cutoff = bank === 0 ? 0.5 : bank === 1 ? 0.75 : 1;
    const kaiserDenominator = bank === 1 ? modifiedBesselOrderZero(KAISER_BETA) : 1;
    const coefficients = new Float64Array(WORDS_PER_BANK);
    let largestRowSum = 0;

    for (let phase = 0; phase < PHASE_COUNT; phase += 1) {
      let rowSum = 0;
      for (let tap = 0; tap < TAPS_PER_PHASE; tap += 1) {
        const gridIndex = 127 - phase + 128 * tap;
        const centeredPosition = 4 * gridIndex / 511 - 2;
        const value = normalizedSinc(cutoff * centeredPosition) *
          bankWindow(bank, gridIndex, kaiserDenominator);
        coefficients[phase * TAPS_PER_PHASE + tap] = value;
        rowSum += value;
      }
      largestRowSum = Math.max(largestRowSum, rowSum);
    }

    const scale = 32767 / largestRowSum;
    for (let word = 0; word < WORDS_PER_BANK; word += 1) {
      const quantized = nearestEven(coefficients[word] * scale);
      putBigEndianWord(table, bank * WORDS_PER_BANK + word, quantized);
    }
  }

  // The fourth bank is deliberately left as the zero-filled allocation above.
  for (const [wordIndex, unsignedValue] of COMPATIBILITY_WORDS) {
    putBigEndianWord(table, wordIndex, unsignedValue);
  }
  return table;
}
