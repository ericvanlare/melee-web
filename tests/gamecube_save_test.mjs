import assert from 'node:assert/strict';
import {createMeleeGCI, parseMeleeGCI, MELEE_GCI_PROFILE_BYTES} from '../web/gamecube-save.mjs';

const profile = Uint8Array.from({length: MELEE_GCI_PROFILE_BYTES}, (_, index) => index * 29 & 0xff);
profile[0] = 0x07; profile[1] = 0xff;
profile[2] = 0x01; profile[3] = 0xc0;
const at = new Date('2026-09-26T20:10:11.000Z');
const {bytes, fileName} = createMeleeGCI(profile, at);
assert.equal(bytes.length, 0x40 + 11 * 0x2000);
assert.equal(fileName, 'melee-save-GALE01-20260926T201011Z.gci');
assert.deepEqual(parseMeleeGCI(bytes), profile);

for (const mutate of [
  copy => { copy[0] = 0x45; },
  copy => { copy[0x39] = 0; },
  copy => { copy[0x40 + 0x2000 + 0x100] ^= 0x80; },
  copy => { copy[0x40 + 9 * 0x2000 + 0x10] ^= 0x01; },
  copy => { copy[0x40 + 10 * 0x2000 + 0x20] ^= 0x01; },
]) {
  const damaged = new Uint8Array(bytes);
  mutate(damaged);
  assert.throws(() => parseMeleeGCI(damaged));
}

console.log('Melee GCI profile extent, checksums, copies, and rejected corruption passed');
