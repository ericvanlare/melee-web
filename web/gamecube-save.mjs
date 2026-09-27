const GCI_HEADER = 0x40;
const CARD_BLOCK = 0x2000;
const BLOCK_COUNT = 11;
const SAVE_BYTES = 0x1790;
const BANK_BYTES = 0x1F2C;
const BANK_COUNT = 7;
const PROFILE_BYTES = SAVE_BYTES + BANK_BYTES * BANK_COUNT;
const INTERNAL_NAME = new TextEncoder().encode('SuperSmashBros0110290334');
const GAME_CODE = new TextEncoder().encode('GALE01');
const KEYS = [0x26, 0xff, 0xe8, 0xef, 0x42, 0xd6, 0x01, 0x54, 0x14, 0xa3, 0x80, 0xfd, 0x6e];
const PERMUTATIONS = [
  [0, 4, 1, 5, 2, 6, 3, 7], [3, 0, 2, 6, 5, 4, 7, 1],
  [6, 5, 0, 1, 3, 7, 2, 4], [1, 3, 7, 4, 0, 2, 5, 6],
  [7, 2, 5, 0, 6, 1, 4, 3], [5, 6, 4, 3, 7, 0, 1, 2],
  [2, 1, 6, 7, 4, 3, 0, 5],
];
const CARD_ENTRIES = [
  [0, 0], [SAVE_BYTES, 0], ...Array.from({length: BANK_COUNT}, () => [BANK_BYTES, 1]),
];
const fail = message => { throw new Error(message); };
class InvalidCardBlock extends Error {
  constructor(message, recoverable = false) {
    super(message);
    this.recoverable = recoverable;
  }
}

function digest(bytes) {
  const state = Uint8Array.from([1, 35, 69, 103, 137, 171, 205, 239, 254, 220, 186, 152, 118, 84, 50, 16]);
  for (let i = 0; i < bytes.length; i++) state[i % 16] = (state[i % 16] + bytes[i]) & 0xff;
  for (let i = 1; i < 16; i++) if (state[i - 1] === state[i]) state[i] ^= 0xff;
  return state;
}

function encodeProtected(input) {
  const out = new Uint8Array(input);
  out.set(digest(out.subarray(16)), 0);
  for (let i = 16; i < out.length; i++) {
    const previous = out[i - 1];
    const key = KEYS[previous % 13];
    const permutation = PERMUTATIONS[previous % 7];
    let transformed = out[i] ^ previous ^ key;
    let encoded = 0;
    for (let bit = 0; bit < 8; bit++) encoded |= ((transformed >> bit) & 1) << permutation[bit];
    out[i] = encoded;
  }
  return out;
}

function decodeProtected(input) {
  if (input.length < 16) throw new InvalidCardBlock('Save file contains a truncated protected card block.');
  const out = new Uint8Array(input);
  let previous = input[15];
  for (let i = 16; i < input.length; i++) {
    const current = input[i];
    const permutation = PERMUTATIONS[previous % 7];
    let bits = 0;
    for (let source = 0; source < 8; source++) bits |= ((current >> permutation[source]) & 1) << source;
    out[i] = bits ^ previous ^ KEYS[previous % 13];
    previous = current;
  }
  const actual = digest(out.subarray(16));
  for (let i = 0; i < 16; i++) if (actual[i] !== out[i])
    throw new InvalidCardBlock('Save file has an invalid Melee card-block checksum.', true);
  return out;
}

function be16(bytes, offset) { return bytes[offset] * 0x100 + bytes[offset + 1]; }
function put16(bytes, offset, value) { bytes[offset] = value >>> 8; bytes[offset + 1] = value; }
function equal(left, right) {
  return left.length === right.length && left.every((value, index) => value === right[index]);
}

// Mirrors HSD's fn_803ACB74 sequence ordering, including the 0xff -> 0 wrap.
function compareSequence(left, right) {
  if (left === 0 && right === 0xff) return 1;
  if (left === 0xff && right === 0) return -1;
  if (left - right > 0x80) return -1;
  if (left - right < -0x80) return 1;
  return left - right;
}

function parseRecordBlocks(gci) {
  const records = new Map(Array.from({length: 8}, (_, index) => [index + 1, []]));
  let freeMarkers = 0;
  let invalidChecksums = 0;
  for (let physical = 1; physical <= 10; physical++) {
    const start = GCI_HEADER + physical * CARD_BLOCK;
    let decoded;
    try {
      decoded = decodeProtected(gci.subarray(start, start + CARD_BLOCK));
    } catch (error) {
      // HSD excludes a bad-checksum block from its logical map. Only that
      // authenticated-format failure is recoverable; truncation and any other
      // parser error remain fatal.
      if (!(error instanceof InvalidCardBlock) || !error.recoverable) throw error;
      invalidChecksums++;
      continue;
    }
    const logical = be16(decoded, 0x10);
    if (logical === 0xffff) {
      freeMarkers++;
      continue;
    }
    if (!records.has(logical)) fail(`Save file physical block ${physical} has an unrecognized Melee data identity.`);
    records.get(logical).push({decoded, sequence: decoded[0x12], physical});
  }

  if (freeMarkers !== 1) fail('Save file does not contain exactly one valid Melee free-block marker.');
  if (records.get(1).length === 0) fail('Save file is missing Melee data record 1.');
  // The authored Melee card manifest has one redundant SaveData chunk. A
  // checksum-invalid block can occupy that redundant slot, but it cannot
  // substitute for a missing logical record or the free-block marker.
  if (records.get(1).length + invalidChecksums !== 2)
    fail('Save file does not contain the expected redundant Melee SaveData record.');

  const selected = new Map();
  for (const [logical, candidates] of records) {
    if (candidates.length === 0) fail(`Save file is missing Melee data record ${logical}.`);
    let newest = candidates[0];
    for (const candidate of candidates.slice(1)) {
      const order = compareSequence(newest.sequence, candidate.sequence);
      if (order < 0) {
        newest = candidate;
      } else if (order === 0 && !equal(
          newest.decoded.subarray(0x20, 0x20 + (logical === 1 ? SAVE_BYTES : BANK_BYTES)),
          candidate.decoded.subarray(0x20, 0x20 + (logical === 1 ? SAVE_BYTES : BANK_BYTES)))) {
        fail(`Save file has conflicting current copies of Melee data record ${logical}.`);
      }
    }
    selected.set(logical, newest.decoded);
  }

  const profile = new Uint8Array(PROFILE_BYTES);
  profile.set(selected.get(1).subarray(0x20, 0x20 + SAVE_BYTES), 0);
  for (let bank = 0; bank < BANK_COUNT; bank++) {
    const bytes = selected.get(bank + 2).subarray(0x20, 0x20 + BANK_BYTES);
    profile.set(bytes, SAVE_BYTES + bank * BANK_BYTES);
  }
  return profile;
}

function cardImageBytes(gci) {
  const banner = gci[7] & 3;
  let size = banner === 1 ? 0x0e00 : banner === 2 ? 0x1800 : 0;
  if (banner !== 0 && banner !== 1 && banner !== 2 && banner !== 3)
    fail('Save file uses an unsupported banner format.');
  const formats = be16(gci, 0x30), speeds = be16(gci, 0x32);
  let sharedPalette = false;
  for (let frame = 0; frame < 8; frame++) {
    const format = (formats >>> (frame * 2)) & 3;
    const speed = (speeds >>> (frame * 2)) & 3;
    if (!speed) {
      if (format) fail('Save file icon header has a format without an animation speed.');
      continue;
    }
    if (format === 1) { size += 0x400; sharedPalette = true; }
    else if (format === 2) size += 0x800;
    else fail('Save file uses an icon format the original Melee card reader does not support.');
  }
  if (sharedPalette) size += 0x200;
  return size;
}

function validateDirectoryHeader(gci) {
  if (!(gci instanceof Uint8Array)) fail('Choose a GameCube .gci save file.');
  if (gci.length !== GCI_HEADER + BLOCK_COUNT * CARD_BLOCK ||
      be16(gci, 0x38) !== BLOCK_COUNT)
    fail('Unsupported save size or block count. Expected an 11-block Melee save file.');
  if (!equal(gci.subarray(0, 6), GAME_CODE))
    fail('This save is not a USA Melee card file (GALE01).');
  if (!equal(gci.subarray(8, 8 + INTERNAL_NAME.length), INTERNAL_NAME))
    fail('This save has an unsupported Melee internal file name.');
  const imageBytes = cardImageBytes(gci);
  const cardHeaderBytes = 0x40 + imageBytes;
  const firstBlockHeader = cardHeaderBytes + 0x30;
  if (firstBlockHeader + 0x30 > CARD_BLOCK)
    fail('Save file banner and icon data exceed the original first card block.');
  const first = gci.subarray(GCI_HEADER, GCI_HEADER + CARD_BLOCK);
  if (!first.subarray(0, 0x40).some(value => value !== 0))
    fail('Save file comment header is empty.');
  const checksum = digest(first.subarray(0, cardHeaderBytes));
  if (!equal(first.subarray(cardHeaderBytes, cardHeaderBytes + 16), checksum))
    fail('Save file metadata checksum is invalid.');
  for (let offset = cardHeaderBytes + 16; offset < firstBlockHeader; offset++)
    if (first[offset] !== 0) fail('Save file first-block integrity padding is invalid.');
  return parseRecordBlocks(gci);
}

export function parseMeleeGCI(gci) {
  return validateDirectoryHeader(gci);
}

function makeProtectedBlock(identity, payload, dataVersion = 0, length = CARD_BLOCK,
  tableIndex = identity) {
  const decoded = new Uint8Array(length);
  put16(decoded, 0x10, identity);
  decoded[0x12] = 0;
  const startIndex = tableIndex === 0 ? 0 :
    (tableIndex + 1 >= CARD_ENTRIES.length || CARD_ENTRIES[tableIndex + 1][0] === 0 ? tableIndex - 2 : tableIndex - 1);
  for (let slot = 0; slot < 3; slot++) {
    const entryIndex = startIndex + slot;
    if (entryIndex < 0 || entryIndex >= CARD_ENTRIES.length) continue;
    const [size, flags] = CARD_ENTRIES[entryIndex];
    const at = 0x13 + slot * 4;
    decoded[at] = entryIndex;
    decoded[at + 1] = ((size >>> 16) & 0x3f) | ((flags << 6) & 0xc0);
    decoded[at + 2] = size >>> 8;
    decoded[at + 3] = size;
  }
  if (payload) decoded.set(payload, 0x20);
  decoded[0x12] = dataVersion;
  return encodeProtected(decoded);
}

function dateParts(date) {
  const pad = value => String(value).padStart(2, '0');
  return {
    fileName: `melee-save-GALE01-${date.getUTCFullYear()}${pad(date.getUTCMonth() + 1)}${pad(date.getUTCDate())}T${pad(date.getUTCHours())}${pad(date.getUTCMinutes())}${pad(date.getUTCSeconds())}Z.gci`,
  };
}

export function createMeleeGCI(profile, date = new Date()) {
  if (!(profile instanceof Uint8Array) || profile.byteLength !== PROFILE_BYTES)
    fail('Save export requires the exact original Melee card profile.');
  const header = new Uint8Array(GCI_HEADER);
  header.set(GAME_CODE, 0);
  header[6] = 0xff;
  header[7] = 0;
  header.set(INTERNAL_NAME, 8);
  const dolphinEpoch = Date.UTC(2000, 0, 1);
  const seconds = Math.max(0, Math.floor((date.getTime() - dolphinEpoch) / 1000));
  new DataView(header.buffer).setUint32(0x28, seconds, false);
  new DataView(header.buffer).setUint32(0x2c, 0x40, false);
  put16(header, 0x30, 0);
  put16(header, 0x32, 0);
  header[0x34] = 0x04;
  header[0x35] = 0;
  put16(header, 0x36, 5);
  put16(header, 0x38, BLOCK_COUNT);
  header[0x3a] = 0xff; header[0x3b] = 0xff;
  new DataView(header.buffer).setUint32(0x3c, 0, false);

  const gci = new Uint8Array(GCI_HEADER + BLOCK_COUNT * CARD_BLOCK);
  gci.set(header, 0);
  const first = gci.subarray(GCI_HEADER, GCI_HEADER + CARD_BLOCK);
  const year = date.getUTCFullYear();
  const month = String(date.getUTCMonth() + 1).padStart(2, '0');
  const day = String(date.getUTCDate()).padStart(2, '0');
  const comment = `Super Smash Bros. Melee         Game Data ${year}/${month}/${day}`;
  first.set(new TextEncoder().encode(comment), 0);
  first.set(digest(first.subarray(0, 0x40)), 0x40);

  for (let identity = 1; identity <= 8; identity++) {
    const payload = identity === 1 ? profile.subarray(0, SAVE_BYTES) :
      profile.subarray(SAVE_BYTES + (identity - 2) * BANK_BYTES,
        SAVE_BYTES + (identity - 1) * BANK_BYTES);
    const encoded = makeProtectedBlock(identity, payload);
    gci.set(encoded, GCI_HEADER + identity * CARD_BLOCK);
  }
  gci.set(makeProtectedBlock(0xffff, null, 0, CARD_BLOCK, 9), GCI_HEADER + 9 * CARD_BLOCK);
  const primary = gci.subarray(GCI_HEADER + CARD_BLOCK, GCI_HEADER + 2 * CARD_BLOCK);
  gci.set(primary, GCI_HEADER + 10 * CARD_BLOCK);
  // The original card writer stores a redundant primary SaveData chunk last.
  const result = parseMeleeGCI(gci);
  if (!equal(result, profile)) fail('Generated GameCube save failed its independent validation pass.');
  return {bytes: gci, fileName: dateParts(date).fileName};
}

export function downloadMeleeGCI(profile, mode) {
  const {bytes, fileName} = createMeleeGCI(profile);
  const url = URL.createObjectURL(new Blob([bytes], {type: 'application/octet-stream'}));
  const link = document.createElement('a');
  link.href = url;
  link.download = fileName;
  link.click();
  setTimeout(() => URL.revokeObjectURL(url), 60_000);
  return {fileName, mode};
}

export const MELEE_GCI_PROFILE_BYTES = PROFILE_BYTES;
