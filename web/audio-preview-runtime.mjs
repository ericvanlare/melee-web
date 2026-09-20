/** Audio-enabled public player. The legacy silent profile excludes this module. */
import {mountMeleeRuntime as mountPlayer} from './melee-runtime.mjs';
import {loadNativeGameDisc, openNativeGameSession} from './runtime-audio-assets.mjs';
import {createRuntimeAudio} from './runtime-audio.mjs';

export const mountMeleeRuntime = options => mountPlayer({
  ...options,
  readDisc: loadNativeGameDisc,
  openDisc: openNativeGameSession,
  createAudio: createRuntimeAudio,
  loaderUrl: new URL('./gameplay_audio_preview.js', import.meta.url),
});
