/** Development audio inputs. This module and its GPL dependency are never public assets. */
import * as disc from './runtime-assets.mjs';
import {replacementDspCoefficients, DSP_COEFFICIENT_SHA256} from './dsp-coefficients.mjs';
export {RUNTIME_DISC_FILES, NATIVE_MENU_DISC_FILES, NATIVE_GAME_DISC_FILES, ORIGINAL_DOL_SHA1} from './runtime-assets.mjs';

async function withAudio(loader, file, report = () => {}) {
  let total = 0;
  const files = await loader(file, progress => {
    total = progress.total + 1;
    report({...progress, total, phase: progress.phase === 'complete' ? 'coefficients' : progress.phase});
  });
  const coefficients = replacementDspCoefficients();
  const digest = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', coefficients)),
    byte => byte.toString(16).padStart(2, '0')).join('');
  if (digest !== DSP_COEFFICIENT_SHA256) throw Error('Generated audio coefficients failed their integrity check.');
  files.set('dsp_coef.bin', coefficients);
  report({phase: 'complete', complete: total, total});
  return files;
}
export const loadRuntimeDisc = (file, report) => withAudio(disc.loadRuntimeDisc, file, report);
export const loadNativeMenuDisc = (file, report) => withAudio(disc.loadNativeMenuDisc, file, report);
export const loadNativeGameDisc = (file, report) => withAudio(disc.loadNativeGameDisc, file, report);
