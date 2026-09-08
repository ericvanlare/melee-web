import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {replacementDspCoefficients,DSP_COEFFICIENT_SHA256} from '../web/dsp-coefficients.mjs';
const first=replacementDspCoefficients(),second=replacementDspCoefficients();
assert.equal(first.byteLength,4096);
assert.equal(createHash('sha256').update(first).digest('hex'),DSP_COEFFICIENT_SHA256);
assert.deepEqual(first,second);first[0]^=255;assert.notDeepEqual(first,second);
console.log('Pinned replacement coefficient generation and independent buffers passed');
